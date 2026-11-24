/* cl_framepace.h -- BDGN 3.51ar: frame pacing options, both off by default.

cl_maxpackets N (BDGN 3.37, 0 = every frame as in id's code; values below 5 count as off): at most N packets per
second to the server, on average. A frame that is not sent is held, and the next command carries:
- the whole held time (msec),
- the time-weighted average of the held forward/side moves (a key pressed only during held frames is not lost),
- the strongest up move (a tapped jump or crouch is not averaged away),
- all buttons, and the impulse (a second impulse forces a send, so none is replaced).
The held command is predicted every frame (CL_PredictMovement), so the view moves every rendered frame; it is the same
command that is later sent, so prediction and server agree. A frame is never held if that could put more than 250 ms
(the byte limit of usercmd_t.msec) into one command. 3.37-3.51aq: the held command was not predicted (view updated at
packet rate), only the newest frame's moves were sent, 30 packets/s gave 25/s with 8 ms frames, and the wait state was
kept across map changes. The state is in client_state_t now (cleared at map change).

cl_sleep 0/1/2 (0 = busy wait as in id's code): while the client waits for its next frame (cl_maxfps), 1 gives the
rest of the time slice away (Sleep(0)), 2 sleeps 1 ms when at least 2 ms are left (Sleep(0) otherwise). Lower CPU load
and heat; with Hyper-Threading the other logical processor gets the time. Sleep(1) can take longer than 1 ms on
Windows 9x, which can make frame times less even there (try 1 first).

Needs usercmd_t (game/q_shared.h). No other engine code (test/framepace_test.c). VS2005 C. */
#ifndef CL_FRAMEPACE_H
#define CL_FRAMEPACE_H

typedef struct
{
	int		msec, buttons, impulse;		/* held since the last packet (impulse may also be carried over a send) */
	int		fwd, side;					/* sum of move * msec of the held frames */
	int		up;							/* strongest up move of the held frames */
	int		lastsend;					/* cls.realtime of the last packet */
	double	next, interval;				/* when the next packet is due and the interval it was planned with (ms) */
	int		pending;					/* a held command exists: predict cl.cmd */
} cl_packetwait_t;

static int CL_PacketAbs (int v) { return v < 0 ? -v : v; }

/* 1 = send this frame's command (cmd) now */
static int CL_PacketDue (cl_packetwait_t *w, const usercmd_t *cmd, int realtime, float maxpackets)
{
	double interval;

	if (maxpackets < 5)
		return 1;
	interval = 1000.0 / maxpackets;
	if (interval != w->interval)
	{	/* first use (send now) or value changed (plan from the last packet with the new interval) */
		w->next = w->interval > 0 ? w->lastsend + interval : 0;
		w->interval = interval;
	}
	if (realtime < w->lastsend)
		return 1;		/* clock went back (new connection) */
	if (w->impulse && cmd->impulse)
		return 1;		/* would replace a held impulse */
	if (w->msec + 2 * cmd->msec > 250)
		return 1;		/* holding could put more than 250 ms into the next command */
	return (double)realtime >= w->next;
}

/* hold cmd; returns the command to predict (exactly what would be sent if this were the last held frame) */
static usercmd_t CL_PacketHold (cl_packetwait_t *w, const usercmd_t *cmd)
{
	usercmd_t shown = *cmd;

	w->fwd += cmd->forwardmove * cmd->msec;
	w->side += cmd->sidemove * cmd->msec;
	if (CL_PacketAbs (cmd->upmove) > CL_PacketAbs (w->up))
		w->up = cmd->upmove;
	w->msec += cmd->msec;
	if (w->msec > 250)
		w->msec = 250;		/* safety; CL_PacketDue sends before this can happen */
	w->buttons |= cmd->buttons;
	if (cmd->impulse)
		w->impulse = cmd->impulse;
	w->pending = 1;
	shown.msec = (byte)w->msec;
	if (w->msec > 0)
	{
		shown.forwardmove = (short)(w->fwd / w->msec);
		shown.sidemove = (short)(w->side / w->msec);
	}
	shown.upmove = (short)w->up;
	shown.buttons = (byte)w->buttons;
	shown.impulse = (byte)w->impulse;
	return shown;
}

/* merge what was held into cmd before it is sent; plan the next packet */
static void CL_PacketRelease (cl_packetwait_t *w, usercmd_t *cmd, int realtime)
{
	int total = cmd->msec + w->msec, carry = 0;

	if (w->pending && total > 0)
	{
		cmd->forwardmove = (short)((w->fwd + cmd->forwardmove * cmd->msec) / total);
		cmd->sidemove = (short)((w->side + cmd->sidemove * cmd->msec) / total);
		if (CL_PacketAbs (w->up) > CL_PacketAbs (cmd->upmove))
			cmd->upmove = (short)w->up;
	}
	cmd->msec = (byte)(total > 250 ? 250 : total);
	cmd->buttons |= (byte)w->buttons;
	if (w->impulse)
	{	/* the held impulse goes now; a new one in this frame goes with the next packet */
		carry = cmd->impulse;
		cmd->impulse = (byte)w->impulse;
	}
	w->msec = w->buttons = w->fwd = w->side = w->up = w->pending = 0;
	w->impulse = carry;
	w->lastsend = realtime;
	/* the next packet is due one interval after this one was due (30/s with 8 ms frames alternates 32 and 40 ms
	   instead of always 40 ms = 25/s); after a late send (more than one interval) start again from now */
	if (realtime < w->next - w->interval || realtime - w->next >= w->interval)
		w->next = realtime + w->interval;
	else
		w->next += w->interval;
}

/* cl_sleep: -1 = no sleep, 0 = Sleep(0), 1 = Sleep(1) */
static int CL_IdleSleep (int mode, int remaining_ms)
{
	if (mode <= 0)
		return -1;
	if (mode == 1)
		return 0;
	return remaining_ms >= 2 ? 1 : 0;
}

#endif
