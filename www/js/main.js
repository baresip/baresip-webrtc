/*
 * main.js
 */

'use strict';

const audio = document.querySelector('audio#audio');
const callButton = document.querySelector('button#callButton');
const disconnectButton = document.querySelector('button#disconnectButton');
const remoteVideo = document.getElementById('remoteVideo');

disconnectButton.disabled = true;
callButton.onclick = connect_call;
disconnectButton.onclick = disconnect_call;


let pc1;
let localStream;  /* MediaStream */


remoteVideo.addEventListener('loadedmetadata', function()
{
	console.log("Remote video videoWidth: %spx,  videoHeight: %spx",
		    this.videoWidth, this.videoHeight);
});


const offerOptions = {
	iceRestart: false,
	voiceActivityDetection: true
};


/*
 * This function is called first.
 */
function connect_call()
{
	callButton.disabled = true;

	console.log("Connecting call");

	const configuration = {
		bundlePolicy: 'balanced',

		/* certificates */

		iceCandidatePoolSize: 0,

		'iceServers': [
		{
			'urls': 'stun:stun.l.google.com:19302'
		}
		],

		iceTransportPolicy: 'all',

		/* peerIdentity */

		rtcpMuxPolicy: 'require',      // NOTE: deprecated
	};

	pc1 = new RTCPeerConnection(configuration);

	console.log("Created local peer connection");

	pc1.onicecandidate = e => onIceCandidate(pc1, e);
	pc1.ontrack = gotRemoteStream;

	pc1.oniceconnectionstatechange = function(event) {
		console.log("ice state changed: %s", pc1.iceConnectionState);
	};

	pc1.onsignalingstatechange = (event) => {
		if (pc1)
			console.log("signaling state: %s", pc1.signalingState);
	};

	pc1.onconnectionstatechange = function(event) {
		console.log("connection state: %s", pc1.connectionState);
	}

	console.log("Requesting local stream");
	navigator.mediaDevices
		.getUserMedia({
			audio: true,
			video: { width:640, height:480, framerate:30 }
			})
		.then(gotStream)
		.catch(e => {
			       alert("getUserMedia() error: ", e.name);
		       });
}


/*
 * MediaStream stream.
 *
 * A stream consists of several tracks such as video or audio tracks.
 */
function gotStream(stream)
{
	disconnectButton.disabled = false;

	// save the stream
	localStream = stream;

	// type: MediaStreamTrack
	const audioTracks = localStream.getAudioTracks();
	const videoTracks = localStream.getVideoTracks();

	if (audioTracks.length > 0) {
		console.log("Using Audio device: '%s'", audioTracks[0].label);
	}
	if (videoTracks.length > 0) {
		console.log("Using Video device: '%s'", videoTracks[0].label);
	}

	localStream.getTracks().forEach(track => pc1.addTrack(track, localStream));

	send_post_connect();
}


function onCreateSessionDescriptionError(error)
{
	console.log("Failed to create session description: %s", error.toString());
}


/*
 * Create a new call
 */
function send_post_connect()
{
	var xhr = new XMLHttpRequest();
	const loc = self.location;

	console.log("send post connect: " + loc);

	xhr.open("POST", '' + loc + 'connect', true);

	xhr.onreadystatechange = function() {
		if (this.readyState === XMLHttpRequest.DONE && this.status === 200) {
			var body = xhr.response;

			pc1.createOffer(offerOptions)
			.then(gotDescription, onCreateSessionDescriptionError);
		}
	}

	xhr.send();
}


function send_put_sdp(descr)
{
	var xhr = new XMLHttpRequest();

	console.log("send put sdp: " + self.location);

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
function gotDescription(desc)
{
	console.log("got local description: %s", desc.type);

	pc1.setLocalDescription(desc)
		.then(() => {
		}, onSetSessionDescriptionError);
}


function disconnect_call()
{
	console.log("Disconnecting call");

	localStream.getTracks().forEach(track => track.stop());

	pc1.close();
	pc1 = null;

	disconnectButton.disabled = true;
	callButton.disabled = false;

	// send a message to the server
	var xhr = new XMLHttpRequest();
	xhr.open("POST", '' + self.location + 'disconnect', true);
	xhr.send();
}


/*
 * RTCTrackEvent event
 */
function gotRemoteStream(event)
{
	const track = event.track;

	console.log("got remote track: kind=%s", track.kind);
	//console.log(event);

	if (audio.srcObject !== event.streams[0]) {
		audio.srcObject = event.streams[0];
		console.log("received remote audio stream");
	}

	if (remoteVideo.srcObject !== event.streams[0]) {
		remoteVideo.srcObject = event.streams[0];
		console.log("received remote video stream");
	}
}


function onIceCandidate(pc, event)
{
	if (event.candidate) {

	} else {
		// All ICE candidates have been sent

		const sd = pc.localDescription;
		const json = JSON.stringify(sd);

		send_put_sdp(json);
	}
}


function onSetSessionDescriptionError(error)
{
	console.log("Failed to set session description: %s", error.toString());
}
