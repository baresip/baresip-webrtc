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


## Protocol Diagram

This diagram shows how a WebRTC capable browser can connect to baresip-webrtc.
Baresip-WebRTC has a small embedded HTTP(S) Server for serving JavaScript files
and for signaling.

The media stream is compatible with WebRTC, using ICE and DTLS/SRTP as
media transport. The audio codecs are Opus, G722 or G711. The video codecs
are VP8, H264.

```
                  (Signaling)
.----------.       SDP/HTTP       .-----------.
| Browser  |<-------------------->|  Baresip  |
| (Chrome) |                      |  WebRTC   |<==== A/V Backend
|          |<====================>|           |
'----------'    ICE/DTLS/SRTP     '-----------'
                (Audio,Video)
```                
                



## API Mapping


| WebRTC:               | this:                      |
| --------------------- | -------------------------- |
| MediaStream           | n/a                        |
| MediaStreamTrack      | struct media_track         |
| RTCConfiguration      | n/a                        |
| RTCPeerConnection     | struct peer_connection     |
| RTCSessionDescription | struct session_description |
| RTCRtpTransceiver     | struct stream              |


