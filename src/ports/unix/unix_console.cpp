/**
 * @file
 * @brief console functions for *nix ports
 */

/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "../../common/common.h"
#include "../system.h"
#include <unistd.h>
#include <signal.h>
#ifndef __vita__
#include <termios.h>
#endif
#include <fcntl.h>
#include <sys/time.h>

typedef struct {
	uint32_t	cursor;
	char		buffer[256];
} consoleHistory_t;

static bool stdinActive;
/* general flag to tell about tty console mode */
static bool ttyConsoleActivated = false;

/* some key codes that the terminal may be using, initialised on start up */
static int TTY_erase;
static int TTY_eof;
#ifndef __vita__
static struct termios TTY_tc;
#endif
static consoleHistory_t ttyConsoleHistory;

/* This is somewhat of a duplicate of the graphical console history
 * but it's safer more modular to have our own here */
#define CON_HISTORY 32
static consoleHistory_t ttyEditLines[CON_HISTORY];
static int histCurrent = -1, histCount = 0;

/**
 * @brief Flush stdin, I suspect some terminals are sending a LOT of shit
 * @todo relevant?
 */
static void CON_FlushIn (void)
{
	char key;
	while (read(STDIN_FILENO, &key, 1) != -1)
		;
}

/**
 * @brief Output a backspace
 * @note it seems on some terminals just sending @c '\\b' is not enough so instead we
 * send @c "\\b \\b"
 * @todo there may be a way to find out if @c '\\b' alone would work though
 */
static void Sys_TTYDeleteCharacter (void)
{
	char key = '\b';
	write(STDOUT_FILENO, &key, 1) || 0;
	key = ' ';
	write(STDOUT_FILENO, &key, 1) || 0;
	key = '\b';
	write(STDOUT_FILENO, &key, 1) || 0;
}

/**
 * @brief Clear the display of the line currently edited
 * bring cursor back to beginning of line
 */
static void Sys_TTYConsoleHide (void)
{
	if (ttyConsoleHistory.cursor > 0) {
		for (unsigned int i = 0; i < ttyConsoleHistory.cursor; i++)
			Sys_TTYDeleteCharacter();
	}
	Sys_TTYDeleteCharacter(); /* Delete "]" */
}

/**
 * @brief Show the current line
 * @todo need to position the cursor if needed?
 */
static void Sys_TTYConsoleShow (void)
{
	write(STDOUT_FILENO, "]", 1) || 0;
	if (ttyConsoleHistory.cursor) {
		for (unsigned int i = 0; i < ttyConsoleHistory.cursor; i++) {
			write(STDOUT_FILENO, ttyConsoleHistory.buffer + i, 1) || 0;
		}
	}
}

static void Sys_TTYConsoleHistoryAdd (consoleHistory_t* field)
{
	const size_t size = lengthof(ttyEditLines);

	assert(histCount <= size);
	assert(histCount >= 0);
	assert(histCurrent >= -1);
	assert(histCurrent <= histCount);
	/* make some room */
	for (int i = size - 1; i > 0; i--)
		ttyEditLines[i] = ttyEditLines[i - 1];

	ttyEditLines[0] = *field;
	if (histCount < size)
		histCount++;

	histCurrent = -1; /* re-init */
}

static consoleHistory_t* Sys_TTYConsoleHistoryPrevious (void)
{
	assert(histCount <= lengthof(ttyEditLines));
	assert(histCount >= 0);
	assert(histCurrent >= -1);
	assert(histCurrent <= histCount);

	const int histPrev = histCurrent + 1;
	if (histPrev >= histCount)
		return nullptr;

	histCurrent++;
	return &(ttyEditLines[histCurrent]);
}

static consoleHistory_t* Sys_TTYConsoleHistoryNext (void)
{
	assert(histCount <= CON_HISTORY);
	assert(histCount >= 0);
	assert(histCurrent >= -1);
	assert(histCurrent <= histCount);
	if (histCurrent >= 0)
		histCurrent--;

	if (histCurrent == -1)
		return nullptr;

	return &(ttyEditLines[histCurrent]);
}

/**
 * @brief Reinitialize console input after receiving SIGCONT, as on Linux the terminal seems to lose all
 * set attributes if user did CTRL+Z and then does fg again.
 */
static void Sys_TTYConsoleSigCont (int signum)
{
	Sys_ConsoleInit();
}

void Sys_ShowConsole (bool show)
{
	static int ttyConsoleHide = 0;

	if (!ttyConsoleActivated)
		return;

	if (show) {
		assert(ttyConsoleHide > 0);
		ttyConsoleHide--;
		if (ttyConsoleHide == 0)
			Sys_TTYConsoleShow();
	} else {
		if (ttyConsoleHide == 0)
			Sys_TTYConsoleHide();
		ttyConsoleHide++;
	}
}

/**
 * @brief Shutdown the console
 * @note Never exit without calling this, or your terminal will be left in a pretty bad state
 */
void Sys_ConsoleShutdown (void)
{
#ifndef __vita__
	if (ttyConsoleActivated) {
		Sys_TTYDeleteCharacter(); /* Delete "]" */
		tcsetattr(STDIN_FILENO, TCSADRAIN, &TTY_tc);
	}

	/* Restore blocking to stdin reads */
	fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) & ~O_NONBLOCK);
#endif
}

static void Sys_TTYConsoleHistoryClear (consoleHistory_t* edit)
{
	OBJZERO(*edit);
}

static bool Sys_IsATTY (void)
{
	const char* term = getenv("TERM");
	return isatty(STDIN_FILENO) && !(term && (Q_streq(term, "raw") || Q_streq(term, "dumb")));
}

/**
 * @brief Initialize the console input (tty mode if possible)
 */
void Sys_ConsoleInit (void)
{
#ifndef __vita__
	/* If the process is backgrounded (running non interactively)
	 * then SIGTTIN or SIGTOU is emitted, if not caught, turns into a SIGSTP */
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);

	/* If SIGCONT is received, reinitialize console */
	signal(SIGCONT, Sys_TTYConsoleSigCont);

	/* Make stdin reads non-blocking */
	fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK);

	if (!Sys_IsATTY()) {
		Com_Printf("tty console mode disabled\n");
		ttyConsoleActivated = false;
		stdinActive = true;
		return;
	}

	Sys_TTYConsoleHistoryClear(&ttyConsoleHistory);
	tcgetattr(STDIN_FILENO, &TTY_tc);
	TTY_erase = TTY_tc.c_cc[VERASE];
	TTY_eof = TTY_tc.c_cc[VEOF];
	struct termios tc = TTY_tc;

	/*
	 * ECHO: don't echo input characters.
	 * ICANON: enable canonical mode. This enables the special characters EOF, EOL,
	 *   EOL2, ERASE, KILL, REPRINT, STATUS, and WERASE, and buffers by lines.
	 * ISIG: when any of the characters INTR, QUIT, SUSP or DSUSP are received,
	 *   generate the corresponding sig­nal.
	 */
	tc.c_lflag &= ~(ECHO | ICANON);

	/*
	 * ISTRIP strip off bit 8
	 * INPCK enable input parity checking
	 */
	tc.c_iflag &= ~(ISTRIP | INPCK);
	tc.c_cc[VMIN] = 1;
	tc.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSADRAIN, &tc);
	ttyConsoleActivated = true;
#endif
}

const char* Sys_ConsoleInput (void)
{
#ifndef __vita__
	/* we use this when sending back commands */
	static char text[256];

	if (ttyConsoleActivated) {
		char key;
		int avail = read(STDIN_FILENO, &key, 1);
		if (avail != -1) {
			/* we have something
			 * backspace?
			 * NOTE TTimo testing a lot of values .. seems it's the only way to get it to work everywhere */
			if (key == TTY_erase || key == 127 || key == 8) {
				if (ttyConsoleHistory.cursor > 0) {
					ttyConsoleHistory.cursor--;
					ttyConsoleHistory.buffer[ttyConsoleHistory.cursor] = '\0';
					Sys_TTYDeleteCharacter();
				}
				return nullptr;
			}
			/* check if this is a control char */
			if (key && key < ' ') {
				if (key == '\n') {
					/* push it in history */
					Sys_TTYConsoleHistoryAdd(&ttyConsoleHistory);
					Q_strncpyz(text, ttyConsoleHistory.buffer, sizeof(text));
					Sys_TTYConsoleHistoryClear(&ttyConsoleHistory);
					key = '\n';
					write(1, &key, 1) || 0;
					write(1, "]", 1) || 0;
					return text;
				}
				if (key == '\t') {
					const size_t size = sizeof(ttyConsoleHistory.buffer);
					const char* s = ttyConsoleHistory.buffer;
					char* target = ttyConsoleHistory.buffer;
					Sys_ShowConsole(false);
					Com_ConsoleCompleteCommand(s, target, size, &ttyConsoleHistory.cursor, 0);
					Sys_ShowConsole(true);
					return nullptr;
				}
				avail = read(STDIN_FILENO, &key, 1);
				if (avail != -1) {
					/* VT 100 keys */
					if (key == '[' || key == 'O') {
						consoleHistory_t* history;
						avail = read(STDIN_FILENO, &key, 1);
						if (avail != -1) {
							switch (key) {
							case 'A':
								history = Sys_TTYConsoleHistoryPrevious();
								if (history) {
									Sys_ShowConsole(false);
									ttyConsoleHistory = *history;
									Sys_ShowConsole(true);
								}
								CON_FlushIn();
								return nullptr;
								break;
							case 'B':
								history = Sys_TTYConsoleHistoryNext();
								Sys_ShowConsole(false);
								if (history) {
									ttyConsoleHistory = *history;
								} else {
									Sys_TTYConsoleHistoryClear(&ttyConsoleHistory);
								}
								Sys_ShowConsole(true);
								CON_FlushIn();
								return nullptr;
								break;
							case 'C':
								return nullptr;
							case 'D':
								return nullptr;
							}
						}
					}
				}
				CON_FlushIn();
				return nullptr;
			}
			if (ttyConsoleHistory.cursor >= sizeof(text) - 1)
				return nullptr;
			/* push regular character */
			ttyConsoleHistory.buffer[ttyConsoleHistory.cursor] = key;
			ttyConsoleHistory.cursor++;
			/* print the current line (this is differential) */
			write(STDOUT_FILENO, &key, 1) || 0;
		}

		return nullptr;
	} else if (stdinActive) {
		fd_set fdset;
		struct timeval timeout;

		FD_ZERO(&fdset);
		FD_SET(STDIN_FILENO, &fdset); /* stdin */
		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		if (select(STDIN_FILENO + 1, &fdset, nullptr, nullptr, &timeout) == -1
				|| !FD_ISSET(STDIN_FILENO, &fdset))
			return nullptr;

		const int len = read(STDIN_FILENO, text, sizeof(text));
		if (len == 0) { /* eof! */
			stdinActive = false;
			return nullptr;
		}

		if (len < 1)
			return nullptr;
		text[len - 1] = 0; /* rip off the /n and terminate */

		return text;
	}
#endif
	return nullptr;
}

void Sys_ConsoleOutput (const char* string)
{
#ifndef __vita__
	/* BUG: for some reason, NDELAY also affects stdout (1) when used on stdin (0). */
	const int origflags = fcntl(STDOUT_FILENO, F_GETFL, 0);

	fcntl(STDOUT_FILENO, F_SETFL, origflags & ~FNDELAY);
	while (*string) {
		const ssize_t written = write(STDOUT_FILENO, string, strlen(string));
		if (written <= 0)
			break; /* sorry, I cannot do anything about this error - without an output */
		string += written;
	}
	fcntl(STDOUT_FILENO, F_SETFL, origflags);
#endif
}
