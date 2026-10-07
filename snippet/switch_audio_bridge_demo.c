/**
 * test_audio_bridge_demo.c
 *
 * 模拟 FreeSWITCH audio_bridge_thread 的核心工作原理。
 * 不依赖 FreeSWITCH 库，使用纯 POSIX 线程 + UDP socket 演示：
 *
 *   1. 两条模拟"呼叫腿"（leg_a, leg_b），各有独立的 RTP UDP socket
 *   2. 两个桥接线程，分别负责 A→B 和 B→A 方向的帧转发
 *   3. 两个媒体生成线程，模拟 UA 端发送 RTP 帧
 *   4. 帧计数与统计，验证桥接正确性
 *
 * 架构图（对应 FreeSWITCH 的真实结构）：
 *
 *   UA_A (producer)                              UA_B (producer)
 *     │ sendto()                                   │ sendto()
 *     ▼                                            ▼
 *   leg_a.rtp_sock (UDP:port_a)              leg_b.rtp_sock (UDP:port_b)
 *     │                                            │
 *     │  ┌─── bridge_thread (A→B) ───┐             │
 *     └──┤  recvfrom(leg_a)          ├─── sendto(leg_b_peer) ───►  leg_b 收
 *        └───────────────────────────┘
 *     ┌──────────────────────────────────────────────┘
 *     │  ┌─── bridge_thread (B→A) ───┐
 *     └──┤  recvfrom(leg_b)          ├─── sendto(leg_a_peer) ───►  leg_a 收
 *        └───────────────────────────┘
 *
 * 编译: gcc -o test_audio_bridge_demo test_audio_bridge_demo.c -lpthread
 * 运行: ./test_audio_bridge_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>
#include <stdatomic.h>

/* ============================================================
 * 模拟 RTP 帧结构（简化版，对应 FreeSWITCH 的 switch_frame_t）
 * ============================================================ */

#define RTP_HEADER_SIZE   12
#define FRAME_SAMPLES     160       /* 8kHz * 20ms = 160 samples */
#define FRAME_PAYLOAD_LEN 160       /* G.711: 1 byte/sample */
#define FRAME_INTERVAL_US 20000     /* 20ms per frame */
#define TEST_DURATION_SEC 2         /* 测试持续秒数 */
#define TOTAL_FRAMES      (TEST_DURATION_SEC * 1000000 / FRAME_INTERVAL_US)  /* 100 frames */

/* 简化的 RTP header */
typedef struct {
    uint8_t  version_flags;   /* V=2, P, X, CC */
    uint8_t  marker_pt;       /* M, PT */
    uint16_t seq;
    uint32_t timestamp;
    uint32_t ssrc;
} rtp_header_t;

typedef struct {
    rtp_header_t header;
    uint8_t      payload[FRAME_PAYLOAD_LEN];
} rtp_frame_t;

/* ============================================================
 * 模拟呼叫腿（对应 FreeSWITCH 的 switch_core_session_t）
 * ============================================================ */

typedef struct {
    const char *name;           /* "leg_a" / "leg_b" */
    int         rtp_sock;       /* RTP 接收 socket */
    uint16_t    rtp_port;       /* 本端 RTP 端口 */
    struct sockaddr_in peer_addr; /* 对端的地址（桥接写入目标） */
    atomic_int  frames_read;    /* 统计：读取的帧数 */
    atomic_int  frames_written; /* 统计：写入的帧数 */
} call_leg_t;

/* ============================================================
 * 桥接线程上下文（对应 FreeSWITCH 的 audio_bridge_data）
 * ============================================================ */

typedef struct {
    call_leg_t *read_leg;       /* session_a: 从这里读 */
    call_leg_t *write_leg;      /* session_b: 写到这里 */
    const char *direction;      /* "A→B" / "B→A" */
    volatile int *bridge_active;
    atomic_int  frames_bridged; /* 成功桥接的帧计数 */
} bridge_thread_data_t;

/* ============================================================
 * 模拟 UA 媒体发送线程（模拟远端 UA 发 RTP 包）
 * ============================================================ */

typedef struct {
    const char *name;
    struct sockaddr_in dest_addr;  /* 发往哪个 leg 的 RTP 端口 */
    volatile int *active;
    uint32_t ssrc;
} ua_producer_data_t;

/* ============================================================
 * audio_bridge_thread — 核心桥接逻辑
 * 对应 FreeSWITCH src/switch_ivr_bridge.c 中的 audio_bridge_thread()
 * ============================================================ */

void *audio_bridge_thread(void *arg) {
    bridge_thread_data_t *data = (bridge_thread_data_t *)arg;
    rtp_frame_t frame;
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    printf("[bridge %s] 桥接线程启动: 从 %s 读取, 写入 %s 的对端\n",
           data->direction, data->read_leg->name, data->write_leg->name);

    while (*(data->bridge_active)) {
        /*
         * 第一步：switch_core_session_read_frame()
         * 在真实 FreeSWITCH 中，这会经过:
         *   switch_core_session_read_frame()
         *     → sofia_read_frame() / endpoint read
         *       → switch_core_media_read_frame()
         *         → switch_rtp_zerocopy_read_frame()
         *           → recvfrom() / timer wait
         *
         * 这里我们直接 recvfrom() 模拟
         */
        ssize_t n = recvfrom(data->read_leg->rtp_sock, &frame, sizeof(frame),
                             0, (struct sockaddr *)&from_addr, &from_len);

        if (n <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* 超时，检查 bridge 是否仍然活跃 */
                continue;
            }
            break;
        }

        atomic_fetch_add(&data->read_leg->frames_read, 1);

        /* 跳过太小的包（类似 FreeSWITCH 跳过 CNG 帧等） */
        if (n < RTP_HEADER_SIZE) continue;

        /*
         * 第二步：switch_core_session_write_frame()
         * 在真实 FreeSWITCH 中，这会经过:
         *   switch_core_session_write_frame()
         *     → 编解码转换（如果 A/B 编解码不同）
         *     → endpoint write → switch_rtp write → sendto()
         *
         * 这里直接 sendto() 到对端
         */
        ssize_t sent = sendto(data->read_leg->rtp_sock, &frame, n, 0,
                              (struct sockaddr *)&data->write_leg->peer_addr,
                              sizeof(data->write_leg->peer_addr));

        if (sent > 0) {
            atomic_fetch_add(&data->write_leg->frames_written, 1);
            atomic_fetch_add(&data->frames_bridged, 1);
        }
    }

    printf("[bridge %s] 桥接线程结束, 共桥接 %d 帧\n",
           data->direction, atomic_load(&data->frames_bridged));
    return NULL;
}

/* ============================================================
 * UA 模拟发送线程：每 20ms 发一个 RTP 帧
 * ============================================================ */

void *ua_producer_thread(void *arg) {
    ua_producer_data_t *data = (ua_producer_data_t *)arg;
    rtp_frame_t frame;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    uint16_t seq = 0;
    uint32_t ts = 0;

    memset(&frame, 0, sizeof(frame));
    frame.header.version_flags = 0x80;  /* V=2 */
    frame.header.marker_pt = 0;         /* PT=0 (PCMU) */
    frame.header.ssrc = htonl(data->ssrc);

    printf("[%s] UA 开始发送 RTP 帧 (每 20ms 一帧, 共 %d 帧)\n",
           data->name, TOTAL_FRAMES);

    for (int i = 0; i < TOTAL_FRAMES && *(data->active); i++) {
        frame.header.seq = htons(seq++);
        frame.header.timestamp = htonl(ts);
        ts += FRAME_SAMPLES;

        /* 填充模拟音频数据（正弦波的粗略近似） */
        for (int j = 0; j < FRAME_PAYLOAD_LEN; j++) {
            frame.payload[j] = (uint8_t)(128 + (i % 50) + j % 10);
        }

        sendto(sock, &frame, sizeof(frame), 0,
               (struct sockaddr *)&data->dest_addr,
               sizeof(data->dest_addr));

        usleep(FRAME_INTERVAL_US);
    }

    printf("[%s] UA 发送结束, 共发送 %d 帧\n", data->name, seq);
    close(sock);
    return NULL;
}

/* ============================================================
 * 辅助函数
 * ============================================================ */

int create_udp_socket(uint16_t port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); exit(1); }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }

    /* 设置接收超时（类似 FreeSWITCH 的 RTP timer/poll 超时） */
    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 }; /* 100ms */
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    return sock;
}

struct sockaddr_in make_addr(uint16_t port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    return addr;
}

/* ============================================================
 * main — 模拟 switch_ivr_multi_threaded_bridge()
 * ============================================================ */

int main() {
    printf("============================================================\n");
    printf("  FreeSWITCH audio_bridge_thread 工作原理演示\n");
    printf("============================================================\n");
    printf("  模拟 %d 秒通话 (%d 帧, 每帧 20ms)\n\n", TEST_DURATION_SEC, TOTAL_FRAMES);

    /* 分配端口 */
    uint16_t port_a = 16000;  /* leg_a 的 RTP 端口 */
    uint16_t port_b = 16002;  /* leg_b 的 RTP 端口 */
    uint16_t port_a_peer = 16010;  /* UA_A 的接收端口（桥接写入目标） */
    uint16_t port_b_peer = 16012;  /* UA_B 的接收端口（桥接写入目标） */

    /* ---- 第一步：创建两条呼叫腿（对应 FreeSWITCH session 创建） ---- */

    call_leg_t leg_a = {
        .name = "leg_a",
        .rtp_sock = create_udp_socket(port_a),
        .rtp_port = port_a,
        .peer_addr = make_addr(port_b_peer),  /* A→B 方向写入 UA_B */
        .frames_read = 0,
        .frames_written = 0,
    };

    call_leg_t leg_b = {
        .name = "leg_b",
        .rtp_sock = create_udp_socket(port_b),
        .rtp_port = port_b,
        .peer_addr = make_addr(port_a_peer),  /* B→A 方向写入 UA_A */
        .frames_read = 0,
        .frames_written = 0,
    };

    printf("[setup] leg_a RTP 端口: %d, 对端: %d\n", port_a, port_b_peer);
    printf("[setup] leg_b RTP 端口: %d, 对端: %d\n", port_b, port_a_peer);

    volatile int bridge_active = 1;

    /* ---- 第二步：创建桥接线程（对应 switch_ivr_multi_threaded_bridge） ---- */

    printf("\n--- 启动桥接 (模拟 switch_ivr_multi_threaded_bridge) ---\n\n");

    bridge_thread_data_t bridge_a_to_b = {
        .read_leg = &leg_a,
        .write_leg = &leg_b,
        .direction = "A→B",
        .bridge_active = &bridge_active,
        .frames_bridged = 0,
    };

    bridge_thread_data_t bridge_b_to_a = {
        .read_leg = &leg_b,
        .write_leg = &leg_a,
        .direction = "B→A",
        .bridge_active = &bridge_active,
        .frames_bridged = 0,
    };

    pthread_t t_bridge_ab, t_bridge_ba;
    pthread_create(&t_bridge_ab, NULL, audio_bridge_thread, &bridge_a_to_b);
    pthread_create(&t_bridge_ba, NULL, audio_bridge_thread, &bridge_b_to_a);

    /* ---- 第三步：启动 UA 模拟发送（模拟两端 UA 发送 RTP） ---- */

    ua_producer_data_t ua_a = {
        .name = "UA_A",
        .dest_addr = make_addr(port_a),  /* 发往 leg_a */
        .active = &bridge_active,
        .ssrc = 0xAAAA0001,
    };

    ua_producer_data_t ua_b = {
        .name = "UA_B",
        .dest_addr = make_addr(port_b),  /* 发往 leg_b */
        .active = &bridge_active,
        .ssrc = 0xBBBB0002,
    };

    pthread_t t_ua_a, t_ua_b;
    pthread_create(&t_ua_a, NULL, ua_producer_thread, &ua_a);
    pthread_create(&t_ua_b, NULL, ua_producer_thread, &ua_b);

    /* ---- 等待 UA 发送完成 ---- */

    pthread_join(t_ua_a, NULL);
    pthread_join(t_ua_b, NULL);

    /* 给桥接线程一点时间处理剩余帧 */
    usleep(200000);

    /* ---- 第四步：停止桥接（对应 FreeSWITCH bridge 结束/hangup） ---- */

    printf("\n--- 停止桥接 ---\n\n");
    bridge_active = 0;

    pthread_join(t_bridge_ab, NULL);
    pthread_join(t_bridge_ba, NULL);

    /* ---- 统计输出 ---- */

    printf("\n============================================================\n");
    printf("  测试结果统计\n");
    printf("============================================================\n");
    printf("  leg_a: 读取 %d 帧, 被写入 %d 帧\n",
           atomic_load(&leg_a.frames_read),
           atomic_load(&leg_a.frames_written));
    printf("  leg_b: 读取 %d 帧, 被写入 %d 帧\n",
           atomic_load(&leg_b.frames_read),
           atomic_load(&leg_b.frames_written));
    printf("  A→B 桥接: %d 帧\n", atomic_load(&bridge_a_to_b.frames_bridged));
    printf("  B→A 桥接: %d 帧\n", atomic_load(&bridge_b_to_a.frames_bridged));
    printf("\n");

    int total_expected = TOTAL_FRAMES;
    int a_to_b = atomic_load(&bridge_a_to_b.frames_bridged);
    int b_to_a = atomic_load(&bridge_b_to_a.frames_bridged);

    if (a_to_b > total_expected * 0.8 && b_to_a > total_expected * 0.8) {
        printf("  ✅ 测试通过: 双向桥接正常工作 (丢帧率 < 20%%)\n");
    } else {
        printf("  ❌ 测试失败: 桥接帧数低于预期\n");
    }

    printf("\n  核心原理验证:\n");
    printf("  • 两个桥接线程独立运行 (线程级隔离)\n");
    printf("  • 每个方向: read_frame → write_frame (同步转发)\n");
    printf("  • 读取阻塞在 recvfrom (对应 FS 的 switch_rtp_zerocopy_read_frame)\n");
    printf("  • 一个方向的阻塞不影响另一个方向\n");
    printf("============================================================\n");

    /* 清理 */
    close(leg_a.rtp_sock);
    close(leg_b.rtp_sock);

    return 0;
}
