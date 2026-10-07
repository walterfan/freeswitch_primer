const EVENT_FIELDS = Object.freeze(["timestamp", "type", "correlation_id", "summary"]);
const METRIC_NAMES = new Set([
  "freeswitch_up",
  "freeswitch_esl_connected",
  "freeswitch_sessions_current",
  "freeswitch_sessions_total",
  "freeswitch_calls_total",
  "freeswitch_call_duration_seconds",
  "freeswitch_heartbeat_age_seconds",
  "freeswitch_ivr_choice_total",
]);

export class BoundedPanel {
  constructor({limit, list, dropped, renderRow}) {
    if (!Number.isInteger(limit) || limit < 1) throw new RangeError("panel limit must be positive");
    if (!list?.replaceChildren || !dropped || typeof renderRow !== "function") {
      throw new TypeError("panel rendering targets are required");
    }
    this.limit = limit;
    this.list = list;
    this.dropped = dropped;
    this.renderRow = renderRow;
    this.rows = [];
    this.droppedCount = 0;
  }

  push(row) {
    this.rows.push(row);
    if (this.rows.length > this.limit) {
      this.rows.splice(0, this.rows.length - this.limit);
      this.droppedCount += 1;
    }
    this.render();
  }

  replace(rows) {
    const safeRows = Array.isArray(rows) ? rows : [];
    this.rows = safeRows.slice(-this.limit);
    this.droppedCount = Math.max(0, safeRows.length - this.rows.length);
    this.render();
  }

  render() {
    this.list.replaceChildren(...this.rows.map(this.renderRow));
    this.dropped.textContent = this.droppedCount
      ? `已丢弃 ${this.droppedCount} 条旧记录（显示上限 ${this.limit}）`
      : `显示上限 ${this.limit} 条`;
    this.dropped.dataset.dropped = String(this.droppedCount);
  }
}

export function formatEvent(payload) {
  if (!payload || typeof payload !== "object") return "invalid event";
  const fields = EVENT_FIELDS.flatMap(name => {
    const value = payload[name];
    if (typeof value !== "string" || !value.trim()) return [];
    return [`${name}=${value.trim().slice(0, 160)}`];
  });
  if (Number.isSafeInteger(payload.drop_count) && payload.drop_count > 0) {
    fields.push(`drop_count=${payload.drop_count}`);
  }
  return fields.length ? fields.join(" · ") : "event received";
}

export function parseMetrics(text) {
  if (typeof text !== "string") return [];
  const rows = [];
  for (const line of text.split(/\r?\n/)) {
    if (!line || line.startsWith("#") || line.length > 512) continue;
    const match = /^([a-zA-Z_:][a-zA-Z0-9_:]*)(?:\{[^}]*\})?\s+([-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?|NaN|[+-]Inf)(?:\s+\d+)?$/.exec(line);
    if (match && METRIC_NAMES.has(match[1])) rows.push(`${match[1]} ${match[2]}`);
  }
  return rows;
}

function textRow(documentRef, value) {
  const item = documentRef.createElement("li");
  item.textContent = value;
  return item;
}

export class LiveEvidenceController {
  constructor({
    eventList,
    eventDropped,
    eventStatus,
    metricsList,
    metricsDropped,
    metricsStatus,
    eventLimit = 100,
    metricsLimit = 20,
    documentRef = globalThis.document,
    EventSourceType = globalThis.EventSource,
    fetchImpl = globalThis.fetch,
  }) {
    this.eventStatus = eventStatus;
    this.metricsStatus = metricsStatus;
    this.EventSourceType = EventSourceType;
    this.fetchImpl = fetchImpl;
    this.events = new BoundedPanel({
      limit: eventLimit,
      list: eventList,
      dropped: eventDropped,
      renderRow: value => textRow(documentRef, value),
    });
    this.metrics = new BoundedPanel({
      limit: metricsLimit,
      list: metricsList,
      dropped: metricsDropped,
      renderRow: value => textRow(documentRef, value),
    });
  }

  connectEvents() {
    if (typeof this.EventSourceType !== "function") {
      this.eventStatus.textContent = "unavailable: 浏览器不支持 EventSource";
      return;
    }
    const source = new this.EventSourceType("/api/v1/events");
    source.onopen = () => { this.eventStatus.textContent = "connected: 正在接收脱敏事件"; };
    source.onmessage = message => {
      try {
        this.events.push(formatEvent(JSON.parse(message.data)));
      } catch (_) {
        this.events.push("invalid event payload");
      }
    };
    source.onerror = () => {
      this.eventStatus.textContent = "unavailable: 事件端点尚未就绪，课程正文不受影响";
      source.close();
    };
    this.eventSource = source;
  }

  async refreshMetrics() {
    if (typeof this.fetchImpl !== "function") {
      this.metricsStatus.textContent = "unavailable: 浏览器不支持 Fetch";
      return;
    }
    try {
      const response = await this.fetchImpl("/metrics", {cache: "no-store"});
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const rows = parseMetrics(await response.text());
      this.metrics.replace(rows);
      this.metricsStatus.textContent = rows.length
        ? "current: 来自教学服务快照"
        : "unavailable: 快照没有已知的 FreeSWITCH 指标";
    } catch (_) {
      this.metricsStatus.textContent = "unavailable: Metrics 端点尚未就绪，课程正文不受影响";
    }
  }

  async start() {
    this.connectEvents();
    await this.refreshMetrics();
  }

  stop() { this.eventSource?.close(); }
}
