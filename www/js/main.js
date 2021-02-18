/*
 * main.js
 */

'use strict';

const audio = document.querySelector('audio#audio');
const callButton = document.querySelector('button#callButton');
const hangupButton = document.querySelector('button#hangupButton');

hangupButton.disabled = true;
callButton.onclick = start_call;
hangupButton.onclick = hangup_call;

const remoteVideo = document.getElementById('remoteVideo');


remoteVideo.addEventListener('loadedmetadata', function() {
  console.log(`Remote video videoWidth: ${this.videoWidth}px,  videoHeight: ${this.videoHeight}px`);
});


let pc1;
let localStream;


const offerOptions = {
  offerToReceiveAudio: 1,
  offerToReceiveVideo: 1,
};


/*
 * This function is called first.
 */
function start_call() {
  callButton.disabled = true;

  console.log('Starting call');

  const configuration = {
    'iceServers': [
      {
        'url': 'stun:stun.l.google.com:19302'
      }
    ],
    iceTransportPolicy: 'all'
  };

  console.log('configuration: ', configuration);

  pc1 = new RTCPeerConnection(configuration);
  console.log('Created local peer connection object pc1');
  pc1.onicecandidate = e => onIceCandidate(pc1, e);
  pc1.ontrack = gotRemoteStream;

  pc1.oniceconnectionstatechange = function(event) {
    console.log(`ice state changed: ${pc1.iceConnectionState}`);
  };

  pc1.onsignalingstatechange = (event) => {
    console.log("---- signalingstate: %s", pc1.signalingState);
  };


  console.log('Requesting local stream');
  navigator.mediaDevices
    .getUserMedia({
      audio: true,
      video: { width:320, height:240, framerate:15 }
    })
    .then(gotStream)
    .catch(e => {
      alert(`getUserMedia() error: ${e.name}`);
    });
}


function gotStream(stream) {
  hangupButton.disabled = false;

  console.log('Received local stream');
  console.log(stream)

  localStream = stream;

  const audioTracks = localStream.getAudioTracks();

  console.log('audio tracks: ' + audioTracks.length);
  if (audioTracks.length > 0) {
    console.log(`Using Audio device: ${audioTracks[0].label}`);
  }

  localStream.getTracks().forEach(track => pc1.addTrack(track, localStream));
  console.log('Adding Local Stream to peer connection');

  send_post_call();
}


function onCreateSessionDescriptionError(error) {
  console.log(`Failed to create session description: ${error.toString()}`);
}


/*
 * Create a new call
 */
function send_post_call() {
  var xhr = new XMLHttpRequest();

  console.log('send post call: ' + self.location);

  xhr.open("POST", '' + self.location + 'call', true);

  xhr.onreadystatechange = function() {
    if (this.readyState === XMLHttpRequest.DONE && this.status === 200) {
      var body = xhr.response;

      if (1) {
        pc1.createOffer(offerOptions)
           .then(gotDescription1, onCreateSessionDescriptionError);
      }
      else {
	      // todo: decode sdp and set remote description
      }
    }
  }

  xhr.send();
}


function send_put_sdp(descr)
{
  var xhr = new XMLHttpRequest();

  console.log('send put sdp: ' + self.location);

  xhr.open("PUT", '' + self.location + 'sdp', true);

  xhr.setRequestHeader("Content-Type", "application/json");

  xhr.onreadystatechange = function() {
    if (this.readyState === XMLHttpRequest.DONE && this.status === 200) {

      const descr = JSON.parse(xhr.response);

      console.log("remote description: type=%s", descr.type);

      pc1.setRemoteDescription(descr).then(() => {
        console.log('set remote description -- success');
      }, onSetSessionDescriptionError);
    }
  }

  xhr.send(descr);
}


/*
 * ${desc.sdp}
 */
function gotDescription1(desc) {
  console.log('set local description');

  pc1.setLocalDescription(desc)
    .then(() => {
    }, onSetSessionDescriptionError);
}


function hangup_call() {
  console.log('Ending call');

  localStream.getTracks().forEach(track => track.stop());

  pc1.close();
  pc1 = null;

  hangupButton.disabled = true;
  callButton.disabled = false;

  // send a message to the server
  var xhr = new XMLHttpRequest();
  xhr.open("POST", '' + self.location + 'hangup', true);
  xhr.send();
}


function gotRemoteStream(e) {

  console.log('got remote stream (track)');
  console.log(e);

  if (audio.srcObject !== e.streams[0]) {
    audio.srcObject = e.streams[0];
    console.log('Received remote stream');
  }

  if (remoteVideo.srcObject !== e.streams[0]) {
      remoteVideo.srcObject = e.streams[0];
         console.log('pc2 received remote stream');
  }
}


function onIceCandidate(pc, event) {

  console.log(`ICE candidate:\n${event.candidate ? event.candidate.candidate : '(null)'}`);

  if (event.candidate) {
	    // Send the candidate to the remote peer
  } else {
     // All ICE candidates have been sent

    const sd = pc.localDescription;
    const json = JSON.stringify(sd);

    send_put_sdp(json);
  }
}


function onSetSessionDescriptionError(error) {
  console.log(`Failed to set session description: ${error.toString()}`);
}
