# baresip-webrtc
Baresip WebRTC Demo

1. Install libre and librem

2. Install baresip dev:

`$ sudo make install install-dev -C ../baresip`

3. Compile this project:

`make`

4. Start it:

```
$ ./baresip-webrtc 
Local network address:  IPv4=en0|10.0.1.12 
medianat: ice
mediaenc: dtls_srtp
aucodec: opus/48000/2
aucodec: G722/16000/1
ausrc: aufile
auplay: aufile
vidcodec: H264
vidcodec: H264
vidcodec: H263
vidcodec: H265
avcodec: using H.264 encoder 'libx264' -- libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10
avcodec: using H.264 decoder 'h264' -- H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10
avcodec: using H.265 encoder 'libx265' -- libx265 H.265 / HEVC
avcodec: using H.265 decoder 'hevc' -- HEVC (High Efficiency Video Coding)
vidsrc: fakevideo
vidisp: fakevideo
ausrc: avformat
vidsrc: avformat
ausrc: ausine
demo: listening on HTTP 0.0.0.0:9000
demo: listening on HTTPS 0.0.0.0:9001
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
| RTCConfiguration      | struct configuration       |
| RTCPeerConnection     | struct peer_connection     |
| RTCSessionDescription | struct session_description |
| RTCRtpTransceiver     | struct stream              |


