
var speed = 16;
var KM_IN_DEGREE = 110.562;
var currentMove;

var center = [38.9983, -76.5496]
var map = L.map('map').setView(center, 10);
map.doubleClickZoom.disable();

L.tileLayer('http://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 18 }).addTo(map);
var marker = L.marker(center).addTo(map);
var moveQueue = [];
var lastDest = null;

map.on('dblclick', function(e) {
	return;
});

map.on('click', function(e) {
	var alt = e.originalEvent.altKey;
	var shift = e.originalEvent.shiftKey;
	var newPos = e.latlng;
	var oldPos = marker.getLatLng();
	
	if (shift && lastDest) {
		oldPos = lastDest;
	}
	
	var deltaLat = (oldPos.lat - newPos.lat) * KM_IN_DEGREE;
	var deltaLng = (oldPos.lng - newPos.lng) * KM_IN_DEGREE;
	
	if (alt) {
		lastDest = null;
		clearQueue();
		move(deltaLat / KM_IN_DEGREE, deltaLng / KM_IN_DEGREE, 1);
		return;
	}
	
	var deltaDist = Math.sqrt((deltaLat * deltaLat) + (deltaLng * deltaLng));
	var deltaSecs = Math.floor(deltaDist / (speed / 3600));

	var rateLat = deltaLat / deltaSecs / KM_IN_DEGREE / 10;
	var rateLng = deltaLng / deltaSecs / KM_IN_DEGREE / 10;
	
	if (!rateLat || !rateLng || !deltaSecs) {
		return;
	}
	
	var line = new L.Polyline([oldPos, newPos], {
		color: 'red',
		weight: 3,
		opacity: 0.5,
		smoothFactor: 1
	});
	
	if (!shift) {
		clearQueue();
	}
	moveQueue.push({rateLat: rateLat, rateLng: rateLng, secondsLeft: deltaSecs * 10, line: line});
	lastDest = newPos;
	line.addTo(map);
})

function clearQueue() {
	for (var i = 0; i < moveQueue.length; i++) {
		map.removeLayer(moveQueue[i].line);
	}
	moveQueue = [];
}

function processQueue() {
	if (moveQueue.length > 0) {
		var item = moveQueue[0];
		move(item.rateLat, item.rateLng);
		item.secondsLeft--;
		
		if (item.secondsLeft <= 0) {
			map.removeLayer(item.line);
			moveQueue.shift();
		}
	}
	
	setTimeout(function() {
		processQueue();
	}, 100);
}

function get() {
	fetch('/api', {
		method: 'post',
		body: JSON.stringify({'action': 'GET'})
	}).then(function(response) {
		return response.json();
	}).then(function(data) {
		marker.setLatLng({lat: data[0], lng: data[1]});
		document.getElementById('position').value = parseFloat(data[0]).toFixed(6) + ', ' + parseFloat(data[1]).toFixed(6);
	});
}

function move(rateLat, rateLng) {
	var oldPos = marker.getLatLng();
	var newPos = {lat: oldPos.lat - rateLat, lng: oldPos.lng - rateLng};
	marker.setLatLng(newPos);
	document.getElementById('position').value = parseFloat(newPos.lat).toFixed(6) + ', ' + parseFloat(newPos.lng).toFixed(6);
	submit(newPos);
}

function submit(newPos) {
	fetch('/api', {
		method: 'post',
		body: JSON.stringify({'action': 'SET', 'pos': newPos})
	}).then(function(response) {
		return response.json();
	}).then(function(data) {
		
	});
}

function updateSpeed() {
	speed = document.getElementById('speed').value;
}

get();
processQueue();
document.getElementById('speed').value = speed;
