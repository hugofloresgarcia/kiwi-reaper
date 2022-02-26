// run me first
b = NetAddr.new("127.0.0.1", 8001);
r = NetAddr.new("127.0.0.1", 8000);

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

// init
(
Routine({
	1.do({
		b.sendMsg("/init", -1);
		1.wait;
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

// get waveform display for 720 pixels at a rate of 0.01
(
Routine({
	10.do({
		arg c;
		b.sendMsg("/get_peaks", c.asFloat, 5.asFloat);
		1.wait;
		c.post; 'sent'.postln;
	});
}).play;
)

OSCFunc.trace(true, true);

OSCFunc.newMatching({|msg, time, addr, recvPort| msg.postln;}, '/rms', r); // path matching

OSCdef(\test, {|msg, time, addr, recvPort| msg.postln}, '/rms', r); // def style
