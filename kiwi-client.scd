// run me first
b = NetAddr.new("127.0.0.1", 8001);

// send a move edit cursor
(
var amt, numtimes, wait_time;
amt = 1.0;
numtimes = 10;
wait_time = 0.01;

Routine({

	numtimes.do({
		b.sendMsg("/move_edit_cursor", amt);
		wait_time.wait;
	});
}).play;
)


// zoom
(
Routine({
	1.do({
		b.sendMsg("/zoom", -1);
		1.wait;
	});
}).play;
)

// get waveform display
(
Routine({
	1.do({
		b.sendMsg("/get_amplitude_at_time", 5.0);
		1.wait;
	});
}).play;
)

OSCFunc.trace(true, true);

OSCFunc.newMatching({|msg, time, addr, recvPort| msg.postln;}, '/rms', b); // path matching
	