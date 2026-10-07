# Day 12

完整步骤见 `tutorial/deploy/README.md`。

```bash
./tutorial/deploy/generate-local-cert.sh
./tutorial/deploy/generate-freeswitch-wss-cert.sh
openssl s_client -connect 10.100.212.8:7443 -servername 10.100.212.8 \
  -CAfile tutorial/deploy/certs/tutorial-lab-ca.crt \
  -verify_ip 10.100.212.8 -verify_return_error </dev/null
```

只导入 CA 证书，不导入私钥。
