# baresip-webrtc
Baresip WebRTC Demo

1. Install libre and librem

2. Install baresip dev:

`$ sudo make install install-dev -C ../baresip`

3. Compile this project:

`cmake . && make`

4. Start it:

```
$ ./baresip-webrtc -i stun:stun.l.google.com:19302
Local network addresses:
        lo0:  fe80::1
        en5:  fe80::aede:48ff:fe00:1122
        en0:  fe80::1025:b8b1:831d:4fa7
        en0:  172.20.10.3
      awdl0:  fe80::141a:90ff:fe24:760d
      utun0:  fe80::8f53:c07e:4132:49ee
      utun1:  fe80::5784:2447:2d94:4f73
      utun2:  fe80::cde3:2f20:c893:1eb1
      utun3:  fe80::c6ef:cfc0:9915:e6f0
      utun4:  fe80::a150:dafb:ecd0:8c19
      utun5:  fe80::680a:1d34:966:5ac0
medianat: ice
mediaenc: dtls_srtp
aucodec: opus/48000/2
aucodec: G722/16000/1
aucodec: PCMU/8000/1
aucodec: PCMA/8000/1
ausrc: ausine
vidcodec: H264
vidcodec: H264
vidcodec: H263
vidcodec: H265
avcodec: using H.264 encoder 'libx264' -- libx264 H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10
avcodec: using H.264 decoder 'h264' -- H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10
avcodec: using H.265 encoder 'libx265' -- libx265 H.265 / HEVC
avcodec: using H.265 decoder 'hevc' -- HEVC (High Efficiency Video Coding)
vidcodec: VP8
vidcodec: VP9
ausrc: avformat
vidsrc: avformat
vidisp: sdl
vidsrc: fakevideo
vidisp: fakevideo
demo: listening on:
    http://172.20.10.3:9000/
    https://172.20.10.3:9001/
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
| RTCConfiguration      | struct rtc_configuration   |
| RTCPeerConnection     | struct peer_connection     |
| RTCSessionDescription | struct session_description |
| RTCRtpTransceiver     | struct stream              |




## Signaling


```


    |             HTTP POST                  |
    +--------------------------------------->+
    |        201 Created (SDP offer)         |
    +<---------------------------------------+
    |                                        |
    |                                        |
    |        HTTP PUT (SDP Answer)           |
    +--------------------------------------->+
    |        200 OK                          |
    +<---------------------------------------+
    |                                        |
    |                                        |
    |        HTTP PATCH (ICE Candidate)      |
    +--------------------------------------->+
    |        200 OK                          |
    +<---------------------------------------+
    |                                        |
    |                                        |
    |                                        |
    |          ICE REQUEST                   |
    <========================================>
    |          ICE RESPONSE                  |
    <========================================>
    |          DTLS SETUP                    |
    <========================================>
    |          RTP/RTCP FLOW                 |
    <========================================>
    |                                        |
    |                                        |
    |                                        |
    |                                        |
    | HTTP DELETE                            |
    +--------------------------------------->+
    | 200 OK                                 |
    <----------------------------------------+


```


## Reference

https://www.ietf.org/archive/id/draft-ietf-wish-whip-03.html
