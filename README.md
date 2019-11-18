# baresip-webrtc
Baresip WebRTC Demo

1. Install libre and librem

2. Install baresip dev:

`$ sudo make install-dev -C ../baresip`

3. Compile this project:

`make`

4. Start it:

```
$ ./baresip-webrtc 
Local network address:  IPv4=en0|10.0.1.8 
medianat: ice
medianat: ice-lite
mediaenc: dtls_srtp
aucodec: PCMU/8000/1
aucodec: PCMA/8000/1
ausrc: aufile
demo: listening on 0.0.0.0:9000
```

5. Open this URL in Chrome and follow the instructions:

`http://localhost:9000/`

