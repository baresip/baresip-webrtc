/*
 * main.js
 */

'use strict';

const connectButton    = document.querySelector('button#connectButton');
const disconnectButton = document.querySelector('button#disconnectButton');
const audio            = document.querySelector('audio#audio');
const remoteVideo      = document.getElementById('remoteVideo');

connectButton.onclick     = connect_call;
disconnectButton.onclick  = disconnect_call;
disconnectButton.disabled = true;


let pc;           /* PeerConnection */
let localStream;  /* MediaStream */


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
};

const constraints = {
	audio: true,
	video: { width:640, height:480, framerate:30 }
};

/*
 * https://developer.mozilla.org/en-US/docs/Web/API/RTCPeerConnection/createOffer#rtcofferoptions_dictionary
 */
const offerOptions = {
	iceRestart:             false,
	voiceActivityDetection: true
};


function connect_call()
{
	connectButton.disabled = true;

	console.log("Connecting call");

	pc = new RTCPeerConnection(configuration);

	pc.onicecandidate = (event) => {

		if (event.candidate) {

		}
		else {
			// All ICE candidates have been sent

			const sd = pc.localDescription;
			const json = JSON.stringify(sd);

			send_put_sdp(json);
		}
	};

	pc.ontrack = function(event) {

		const track = event.track;

		console.log("got remote track: kind=%s", track.kind);

		if (audio.srcObject !== event.streams[0]) {
			audio.srcObject = event.streams[0];
			console.log("received remote audio stream");
		}

		if (remoteVideo.srcObject !== event.streams[0]) {
			remoteVideo.srcObject = event.streams[0];
			console.log("received remote video stream");
		}
	};

	console.log("Requesting local stream");

	navigator.mediaDevices.getUserMedia(constraints)
		.then(function(stream) {

			disconnectButton.disabled = false;

			// save the stream
			localStream = stream;

			// type: MediaStreamTrack
			const audioTracks = localStream.getAudioTracks();
			const videoTracks = localStream.getVideoTracks();

			if (audioTracks.length > 0) {
				console.log("Using Audio device: '%s'",
					    audioTracks[0].label);
			}
			if (videoTracks.length > 0) {
				console.log("Using Video device: '%s'",
					    videoTracks[0].label);
			}

			localStream.getTracks()
				.forEach(track => pc.addTrack(track, localStream));

			send_post_connect();
		})
		.catch(function(err) {
			/* handle the error */
		});
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

			pc.createOffer(offerOptions)
			.then(function (desc) {
				console.log("got local description: %s", desc.type);

				pc.setLocalDescription(desc).then(() => {
				},
				function (error) {
					console.log("setLocalDescription: %s",
						    error.toString());
				});
			})
			.catch(function(error) {
			       console.log("Failed to create session description: %s",
					   error.toString());
			});
		}
	}

	xhr.send();
}


function send_put_sdp(descr)
{
	var xhr = new XMLHttpRequest();
	const loc = self.location;

	console.log("send put sdp: " + loc);

	xhr.open("PUT", '' + loc + 'sdp', true);
	xhr.setRequestHeader("Content-Type", "application/json");

	xhr.onreadystatechange = function() {
		if (this.readyState === XMLHttpRequest.DONE && this.status === 200) {

			const descr = JSON.parse(xhr.response);

			console.log("remote description: type=%s", descr.type);

			pc.setRemoteDescription(descr).then(() => {
				console.log('set remote description -- success');
			}, function (error) {
				console.log("setRemoteDescription: %s",
					    error.toString());
			});
		}
	}

	xhr.send(descr);
}


function disconnect_call()
{
	console.log("Disconnecting call");

	localStream.getTracks().forEach(track => track.stop());

	pc.close();
	pc = null;

	disconnectButton.disabled = true;
	connectButton.disabled = false;

	// send a message to the server
	var xhr = new XMLHttpRequest();
	xhr.open("POST", '' + self.location + 'disconnect', true);
	xhr.send();
}
