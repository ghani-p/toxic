/*  prompt.c
 *
 *
 *  Copyright (C) 2014 Toxic All Rights Reserved.
 *
 *  This file is part of Toxic.
 *
 *  Toxic is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Toxic is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Toxic.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "toxic.h"
#include "windows.h"
#include "prompt.h"
#include "execute.h"
#include "misc_tools.h"
#include "toxic_strings.h"
#include "log.h"
#include "line_info.h"
#include "settings.h"
#include "input.h"

uint8_t pending_frnd_requests[MAX_FRIENDS_NUM][TOX_CLIENT_ID_SIZE] = {0};
uint8_t num_frnd_requests = 0;
extern ToxWindow *prompt;
struct _Winthread Winthread;

extern struct user_settings *user_settings;

/* Array of global command names used for tab completion. */
const uint8_t glob_cmd_list[AC_NUM_GLOB_COMMANDS][MAX_CMDNAME_SIZE] = {
    { "/accept"     },
    { "/add"        },
    { "/clear"      },
    { "/close"      },    /* rm /close when groupchats gets its own list */
    { "/connect"    },
    { "/exit"       },
    { "/groupchat"  },
    { "/help"       },
    { "/join"       },
    { "/log"        },
    { "/myid"       },
    { "/nick"       },
    { "/note"       },
    { "/quit"       },
    { "/status"     },

#ifdef _SUPPORT_AUDIO

    { "/lsdev"       },
    { "/sdev"        },

#endif /* _SUPPORT_AUDIO */
};

/* Updates own nick in prompt statusbar */
void prompt_update_nick(ToxWindow *prompt, uint8_t *nick, uint16_t len)
{
    StatusBar *statusbar = prompt->stb;
    snprintf(statusbar->nick, sizeof(statusbar->nick), "%s", nick);
    statusbar->nick_len = strlen(statusbar->nick);
}

/* Updates own statusmessage in prompt statusbar */
void prompt_update_statusmessage(ToxWindow *prompt, uint8_t *statusmsg, uint16_t len)
{
    StatusBar *statusbar = prompt->stb;
    snprintf(statusbar->statusmsg, sizeof(statusbar->statusmsg), "%s", statusmsg);
    statusbar->statusmsg_len = strlen(statusbar->statusmsg);
}

/* Updates own status in prompt statusbar */
void prompt_update_status(ToxWindow *prompt, uint8_t status)
{
    StatusBar *statusbar = prompt->stb;
    statusbar->status = status;
}

/* Updates own connection status in prompt statusbar */
void prompt_update_connectionstatus(ToxWindow *prompt, bool is_connected)
{
    StatusBar *statusbar = prompt->stb;
    statusbar->is_online = is_connected;
}

/* Adds friend request to pending friend requests.
   Returns request number on success, -1 if queue is full or other error. */
static int add_friend_request(const uint8_t *public_key)
{
    if (num_frnd_requests >= MAX_FRIENDS_NUM)
        return -1;

    int i;

    for (i = 0; i <= num_frnd_requests; ++i) {
        if (!strlen(pending_frnd_requests[i])) {
            memcpy(pending_frnd_requests[i], public_key, TOX_CLIENT_ID_SIZE);

            if (i == num_frnd_requests)
                ++num_frnd_requests;

            return i;
        }
    }

    return -1;
}

static void prompt_onKey(ToxWindow *self, Tox *m, wint_t key, bool ltr)
{
    ChatContext *ctx = self->chatwin;

    int x, y, y2, x2;
    getyx(self->window, y, x);
    getmaxyx(self->window, y2, x2);

    if (x2 <= 0)
        return;

    if (ltr) {    /* char is printable */
        input_new_char(self, key, x, y, x2, y2);
        return;
    }

    if (line_info_onKey(self, key))
        return;

    input_handle(self, key, x, y, x2, y2);

    if (key == '\t') {    /* TAB key: auto-completes command */
        if (ctx->len > 1 && ctx->line[0] == '/') {
            int diff = complete_line(ctx, glob_cmd_list, AC_NUM_GLOB_COMMANDS, MAX_CMDNAME_SIZE);

            if (diff != -1) {
                if (x + diff > x2 - 1) {
                    wmove(self->window, y, x + diff);
                    ctx->start += diff;
                } else {
                    wmove(self->window, y, x + diff);
                }
            } else {
                beep();
            }
        } else {
            beep();
        }
    } else if (key == '\n') {
        rm_trailing_spaces_buf(ctx);

        uint8_t line[MAX_STR_SIZE] = {0};

        if (wcs_to_mbs_buf(line, ctx->line, MAX_STR_SIZE) == -1)
            memset(&line, 0, sizeof(line));

        if (!string_is_empty(line))
            add_line_to_hist(ctx);

        line_info_add(self, NULL, NULL, NULL, line, PROMPT, 0, 0);
        execute(ctx->history, self, m, line, GLOBAL_COMMAND_MODE);

        wclear(ctx->linewin);
        wmove(self->window, y2 - CURS_Y_OFFSET, 0);
        reset_buf(ctx);
    }
}

static void prompt_onDraw(ToxWindow *self, Tox *m)
{
    int x2, y2;
    getmaxyx(self->window, y2, x2);

    ChatContext *ctx = self->chatwin;

    line_info_print(self);
    wclear(ctx->linewin);

    curs_set(1);

    if (ctx->len > 0)
        mvwprintw(ctx->linewin, 1, 0, "%ls", &ctx->line[ctx->start]);

    StatusBar *statusbar = self->stb;
    mvwhline(statusbar->topline, 1, 0, ACS_HLINE, x2);
    wmove(statusbar->topline, 0, 0);

    if (statusbar->is_online) {
        int colour = WHITE;
        const uint8_t *status_text = "Unknown";

        switch (statusbar->status) {
            case TOX_USERSTATUS_NONE:
                status_text = "Online";
                colour = GREEN;
                break;

            case TOX_USERSTATUS_AWAY:
                status_text = "Away";
                colour = YELLOW;
                break;

            case TOX_USERSTATUS_BUSY:
                status_text = "Busy";
                colour = RED;
                break;

            case TOX_USERSTATUS_INVALID:
                status_text = "ERROR";
                colour = MAGENTA;
                break;
        }

        wattron(statusbar->topline, COLOR_PAIR(colour) | A_BOLD);
        wprintw(statusbar->topline, " [%s]", status_text);
        wattroff(statusbar->topline, COLOR_PAIR(colour) | A_BOLD);

        wattron(statusbar->topline, A_BOLD);
        wprintw(statusbar->topline, " %s", statusbar->nick);
        wattroff(statusbar->topline, A_BOLD);
    } else {
        wprintw(statusbar->topline, "[Offline]");
        wattron(statusbar->topline, A_BOLD);
        wprintw(statusbar->topline, " %s ", statusbar->nick);
        wattroff(statusbar->topline, A_BOLD);
    }

    if (statusbar->statusmsg[0])
        wprintw(statusbar->topline, " - %s", statusbar->statusmsg);

    mvwhline(ctx->linewin, 0, 0, ACS_HLINE, x2);
}

static void prompt_onConnectionChange(ToxWindow *self, Tox *m, int32_t friendnum , uint8_t status)
{
    if (friendnum < 0)
        return;

    ChatContext *ctx = self->chatwin;

    uint8_t nick[TOX_MAX_NAME_LENGTH] = {0};
    int n_len = tox_get_name(m, friendnum, nick);
    n_len = MIN(n_len, TOXIC_MAX_NAME_LENGTH - 1);

    if (!nick[0]) {
        snprintf(nick, sizeof(nick), "%s", UNKNOWN_NAME);
        n_len = strlen(UNKNOWN_NAME);
    }

    nick[n_len] = '\0';

    uint8_t timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));
    uint8_t *msg;

    if (status == 1) {
        msg = "has come online";
        line_info_add(self, timefrmt, nick, NULL, msg, CONNECTION, 0, GREEN);
        write_to_log(msg, nick, ctx->log, true);
        alert_window(self, WINDOW_ALERT_2, false);
    } else {
        msg = "has gone offline";
        line_info_add(self, timefrmt, nick, NULL, msg, CONNECTION, 0, RED);
        write_to_log(msg, nick, ctx->log, true);
    }
}

static void prompt_onFriendRequest(ToxWindow *self, Tox *m, const uint8_t *key, const uint8_t *data,
                                   uint16_t length)
{
    ChatContext *ctx = self->chatwin;

    uint8_t timefrmt[TIME_STR_SIZE];
    get_time_str(timefrmt, sizeof(timefrmt));

    uint8_t msg[MAX_STR_SIZE];
    snprintf(msg, sizeof(msg), "Friend request with the message '%s'", data);
    line_info_add(self, timefrmt, NULL, NULL, msg, SYS_MSG, 0, 0);
    write_to_log(msg, "", ctx->log, true);

    int n = add_friend_request(key);

    if (n == -1) {
        uint8_t *errmsg = "Friend request queue is full. Discarding request.";
        line_info_add(self, NULL, NULL, NULL, errmsg, SYS_MSG, 0, 0);
        write_to_log(errmsg, "", ctx->log, true);
        return;
    }

    snprintf(msg, sizeof(msg), "Type \"/accept %d\" to accept it.", n);
    line_info_add(self, NULL, NULL, NULL, msg, SYS_MSG, 0, 0);
    alert_window(self, WINDOW_ALERT_1, true);
}

void prompt_init_statusbar(ToxWindow *self, Tox *m)
{
    int x2, y2;
    getmaxyx(self->window, y2, x2);

    /* Init statusbar info */
    StatusBar *statusbar = self->stb;
    statusbar->status = TOX_USERSTATUS_NONE;
    statusbar->is_online = false;

    uint8_t nick[TOX_MAX_NAME_LENGTH];
    uint8_t statusmsg[MAX_STR_SIZE];

    pthread_mutex_lock(&Winthread.lock);
    uint16_t n_len = tox_get_self_name(m, nick);
    uint16_t s_len = tox_get_self_status_message(m, statusmsg, MAX_STR_SIZE);
    uint8_t status = tox_get_self_user_status(m);
    pthread_mutex_unlock(&Winthread.lock);

    nick[n_len] = '\0';
    statusmsg[s_len] = '\0';

    /* load prev status message or show toxic version if it has never been set */
    uint8_t ver[strlen(TOXICVER) + 1];
    strcpy(ver, TOXICVER);
    const uint8_t *toxic_ver = strtok(ver, "_");

    if ( (!strcmp("Online", statusmsg) || !strncmp("Toxing on Toxic", statusmsg, 15)) && toxic_ver != NULL) {
        snprintf(statusmsg, MAX_STR_SIZE, "Toxing on Toxic v.%s", toxic_ver);
        s_len = strlen(statusmsg);
        statusmsg[s_len] = '\0';
    }

    prompt_update_statusmessage(prompt, statusmsg, s_len);
    prompt_update_status(prompt, status);
    prompt_update_nick(prompt, nick, n_len);

    /* Init statusbar subwindow */
    statusbar->topline = subwin(self->window, 2, x2, 0, 0);
}

static void prompt_onInit(ToxWindow *self, Tox *m)
{
    curs_set(1);
    int y2, x2;
    getmaxyx(self->window, y2, x2);

    ChatContext *ctx = self->chatwin;
    ctx->history = subwin(self->window, y2 - CHATBOX_HEIGHT + 1, x2, 0, 0);
    ctx->linewin = subwin(self->window, CHATBOX_HEIGHT, x2, y2 - CHATBOX_HEIGHT, 0);

    ctx->log = malloc(sizeof(struct chatlog));
    ctx->hst = malloc(sizeof(struct history));

    if (ctx->log == NULL || ctx->hst == NULL)
        exit_toxic_err("failed in prompt_onInit", FATALERR_MEMORY);

    memset(ctx->log, 0, sizeof(struct chatlog));
    memset(ctx->hst, 0, sizeof(struct history));

    line_info_init(ctx->hst);

    if (user_settings->autolog == AUTOLOG_ON) {
        uint8_t myid[TOX_FRIEND_ADDRESS_SIZE];
        tox_get_address(m, myid);
        log_enable(self->name, myid, ctx->log);
    }

    execute(ctx->history, self, m, "/help", GLOBAL_COMMAND_MODE);
    scrollok(ctx->history, 0);
    wmove(self->window, y2 - CURS_Y_OFFSET, 0);
}

ToxWindow new_prompt(void)
{
    ToxWindow ret;
    memset(&ret, 0, sizeof(ret));

    ret.active = true;
    ret.is_prompt = true;

    ret.onKey = &prompt_onKey;
    ret.onDraw = &prompt_onDraw;
    ret.onInit = &prompt_onInit;
    ret.onConnectionChange = &prompt_onConnectionChange;
    ret.onFriendRequest = &prompt_onFriendRequest;

    strcpy(ret.name, "prompt");

    ChatContext *chatwin = calloc(1, sizeof(ChatContext));
    StatusBar *stb = calloc(1, sizeof(StatusBar));

    if (stb == NULL || chatwin == NULL)
        exit_toxic_err("failed in new_prompt", FATALERR_MEMORY);

    ret.chatwin = chatwin;
    ret.stb = stb;

    return ret;
}
