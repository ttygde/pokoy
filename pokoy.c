#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#include <limits.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include <err.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_icccm.h>
#include <xcb/screensaver.h>


#define ever						;;
#define NAME						"pokoy"
#define VERSION						"0.1"
#define PID_FILE					"/pokoy.pid"
#define CONFIG_NAME					NAME "rc"
#define CONFIG_HOME_ENV				"XDG_CONFIG_HOME"
#define RUNTIME_HOME_ENV			"XDG_RUNTIME_DIR"
#define START_COMMENT				'#'
#define NUMBER_OF_HASH_SYMBOLS		50
#define ONE_MINUTE					60
#define FLAG_BAR					1
#define FLAG_TIMER					2
#define FLAG_ENABLE_SKIP			4
#define FLAG_ENABLE_POSTPONE		8
#define FLAG_DEBUG					16
#define FLAG_BLOCK					32
#define FLAG_IDLE					64


typedef struct {
	uint32_t		it;  //intermediate time
	uint32_t		du;  // duration
	uint32_t		pt;  // postpone time
	time_t			rt;  // remaining time 
} cbreak;


struct x_context {
	xcb_connection_t *c;
	xcb_gcontext_t g;
	xcb_screen_t *s;
	xcb_key_symbols_t *symbols;
};


volatile sig_atomic_t now = 0;
volatile sig_atomic_t show = 0;
static char *config_path;
static char *font;
static uint32_t number_of_breaks = 0;
static uint32_t flags = 0;
static uint32_t width_bar = 0;
static uint32_t bar_x = 0;
static uint32_t bar_y = 0;
static uint32_t width_timer = 0;
static uint32_t timer_x = 0;
static uint32_t timer_y = 0;
static struct x_context xc;
static FILE *fp;
static uint32_t nb = 0;
static char **blacklist;
static cbreak **cbreaks;
static uint32_t idle_time;


void load_config();
void init_daemon();
void init_x_context();
void create_cb(cbreak *); 
static void signal_handler(int);
void cleanup();
uint8_t is_idle();


static uint8_t
im_daemon() {
	xcb_get_input_focus_reply_t *ifr;
	xcb_icccm_get_wm_class_reply_t t;
	xcb_get_property_cookie_t c;
	uint8_t idle_counter = 0;
	uint32_t postpone_time;

	font = malloc(200);
	cbreaks = malloc(sizeof(char *) * 20);
	blacklist = malloc(sizeof(char *) * 20);

	load_config();
	init_daemon();
	init_x_context();

	syslog(LOG_INFO, "Starting daemon.");
	for (uint8_t u = 0; u < nb; u++) {
		syslog(LOG_DEBUG, "%s added to blacklist.", blacklist[u]);
	}
	syslog(LOG_DEBUG, "Idle time: %d", idle_time);
	syslog(LOG_DEBUG, "Flags: %d", flags);

	uint32_t i, j;
	for (ever) {
		for (i = 0; i < number_of_breaks; i++) {
			syslog(LOG_DEBUG, "time(0): %d, rt: %d, it: %d, d: %d, pt: %d\n", 
					(int)time(0), (int)cbreaks[i]->rt, cbreaks[i]->it, cbreaks[i]->du, cbreaks[i]->pt);
			if (difftime(cbreaks[i]->rt, time(0)) <= 0) {
				if (nb > 0) { // if there is something in blacklist
					ifr = xcb_get_input_focus_reply(xc.c, xcb_get_input_focus(xc.c), NULL);
					c = xcb_icccm_get_wm_class(xc.c, ifr->focus);
					if (xcb_icccm_get_wm_class_reply(xc.c, c, &t, NULL)) {
						syslog (LOG_DEBUG, "Instance: %s\n", t.instance_name);
						syslog (LOG_DEBUG, "Class: %s\n", t.class_name);
						for (j = 0; j < nb; j++) {
							if ((strcmp(blacklist[j], t.class_name)) == 0) {
								flags |= FLAG_BLOCK;
								cbreaks[i]->rt += cbreaks[i]->it / 2;
								syslog(LOG_DEBUG, "%s has focus. Skipping.", t.class_name);
								break;
							} 
						}
						xcb_icccm_get_wm_class_reply_wipe(&t);
					}
					free(ifr);
				}
				if (flags ^ FLAG_BLOCK) {
					cbreaks[i]->rt = INT_MAX;
					create_cb(cbreaks[i]);
					// check other breaks and postpone them
					for (j = 0; j < number_of_breaks; j++) {
						if (j == i) continue;
						syslog(LOG_DEBUG, "Correction.");
						if (difftime(cbreaks[j]->rt - ONE_MINUTE, time(0)) <= 0) {
							postpone_time = cbreaks[j]->it / 5;
							while (cbreaks[j]->rt < (time(0) + postpone_time))
								cbreaks[j]->rt += postpone_time;
						}
					}
				}
				flags &= ~FLAG_BLOCK;
			}
			if (idle_counter > 30) {
				if (is_idle()) {
					syslog(LOG_DEBUG, "It seems that there is nobody. Sleeping.");
					while (is_idle()) {
						sleep(30);
					}
					for (j = 0; j < number_of_breaks; j++) {
						cbreaks[j]->rt = time(0) + cbreaks[j]->it;
					}
					syslog(LOG_DEBUG, "Awakening.");
				}
				idle_counter = 0;
			} else idle_counter++;
		}
		syslog (LOG_DEBUG, "--");
		if (now) {
			create_cb(cbreaks[0]);
			now = 0;	
		}
		if (show) {
			fseek(fp, 4, SEEK_SET);
			for (i = 0; i < number_of_breaks; i++) 
				fwrite(&(cbreaks[i]->rt), 4, 1, fp);
			fflush(fp);
			show = 0;
		}
		sleep(1);
	}
	return 0;
}


uint8_t
is_idle() {
	xcb_screensaver_query_info_cookie_t sqic;
	xcb_screensaver_query_info_reply_t *sqir;
	sqic = xcb_screensaver_query_info(xc.c, xc.s->root);	
	sqir = xcb_screensaver_query_info_reply(xc.c, sqic, NULL);
	syslog(LOG_DEBUG, "Idle time: %d", sqir->ms_since_user_input / 1000);
	if ((sqir->ms_since_user_input / 1000) > idle_time) 
		return 1;
	else 
		return 0;
}


void
load_config() {
	flags |= FLAG_ENABLE_POSTPONE | FLAG_ENABLE_SKIP;
	idle_time = 600;
	if (config_path[0] == '\0') {
		char *config_home = getenv(CONFIG_HOME_ENV);
		if (config_home != NULL)
			sprintf(config_path, "%s/%s/%s", config_home, NAME, CONFIG_NAME);
		else
			sprintf(config_path, "%s/%s/%s/%s", getenv("HOME"), ".config", NAME, CONFIG_NAME);
	}

	FILE *cfg = fopen(config_path, "r");
	if (cfg == NULL) {
		err(1, "Can't open configuration file: '%s'", config_path);	
	}
	char *buf = malloc(200);
	char first;
	char *p, *k;
	while (fgets(buf, 200, cfg) != NULL) {
		first = buf[0];
		if (strlen(buf) < 2 || first == START_COMMENT) continue;
		k = strtok(buf, " \t\n:");
		p = strtok(NULL, " \t\n:");

		if (strcmp(k, "break") == 0) {
			cbreak *cb = malloc(sizeof(cbreak));
			cb->it = atoi(p) * 60 + atoi(strtok(NULL, " \t:"));
			cb->du = atoi(strtok(NULL, " \t:")) * 60 + atoi(strtok(NULL, " \t:"));
			cb->pt = atoi(strtok(NULL, " \t:")) * 60 + atoi(strtok(NULL, " \t:"));
			cb->rt = time(0) + cb->it;
			cbreaks[number_of_breaks] = cb;
			number_of_breaks++;
		} else if (strcmp(k, "show_bar") == 0) {
			if (strcmp(p, "false") == 0) flags |= FLAG_BAR;
		} else if (strcmp(k, "show_timer") == 0) {
			if (strcmp(p, "false") == 0) flags |= FLAG_TIMER;
		} else if (strcmp(k, "enable_skip") == 0) {
			if (strcmp(p, "false") == 0) flags &= ~FLAG_ENABLE_SKIP;
		} else if (strcmp(k, "enable_postpone") == 0) {
			if (strcmp(p, "false") == 0) flags &= ~FLAG_ENABLE_POSTPONE;
		} else if (strcmp(k, "font") == 0) {
			strcpy(font, p);
		} else if (strcmp(k, "block") == 0) {
			char *block = malloc(sizeof(p));
			strcpy(block, p);
			blacklist[nb] = block;
			nb++;
		} else if (strcmp(k, "idle_time") == 0) {
			idle_time = atoi(p) * 60 + atoi(strtok(NULL, " \t:"));
		}
	}
	fclose(cfg);
	free(buf);
	free(config_path);
}

void
init_daemon() {
	uint32_t p = (uint32_t)getpid();
	fwrite(&p, 1, 4, fp);
	flock(fileno(fp), LOCK_EX | LOCK_NB);
	fflush(fp);

	openlog(NAME, LOG_PID, LOG_USER);
	if ((flags & FLAG_DEBUG) == 0) setlogmask(LOG_UPTO(LOG_INFO));

	umask(0);
	setsid();
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
	
	atexit(cleanup);

	struct sigaction sa;
	sa.sa_handler = signal_handler;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGTERM, &sa, NULL) == -1 || sigaction(SIGUSR1, &sa, NULL) == -1 || sigaction(SIGUSR2, &sa, NULL) == -1) {
		syslog(LOG_ERR, "Cannot change signal action.");
	}
}


void 
init_x_context() {
	uint32_t mask, values[2];

	xc.c = xcb_connect (NULL, NULL);

	if (xc.c == NULL) {
		syslog(LOG_ERR, "Cannot open display\n");
		exit(1);
	}
	xc.s = xcb_setup_roots_iterator (xcb_get_setup (xc.c)).data;


	// get font
	xcb_void_cookie_t cookie;
	xcb_font_t font_id = xcb_generate_id (xc.c);
	cookie = xcb_open_font_checked(xc.c, font_id, strlen(font), font);
	if (xcb_request_check (xc.c, cookie)) {
		syslog(LOG_WARNING, "Could not load font %s.\nTrying fixed font.", font);
		memset(font, '\0', strlen(font));
		strcpy(font, "fixed");
		cookie = xcb_open_font_checked(xc.c, font_id, strlen(font), font);
		if (xcb_request_check (xc.c, cookie)) {
			syslog(LOG_ERR, "Could not load fixed font. Aborting");
		}
	} 
	
	// create graphics context
	xc.g = xcb_generate_id(xc.c);
	mask = XCB_GC_FOREGROUND | XCB_GC_FONT;
	values[0] = xc.s->white_pixel;
	values[1] = font_id;
	xcb_create_gc(xc.c, xc.g, xc.s->root, mask, values);
	xcb_close_font(xc.c, font_id);

	/*[> Allocate key symboll <]*/
	xc.symbols = xcb_key_symbols_alloc(xc.c);
	free(font);
}


static void 
signal_handler(int sig) {
	if (sig == SIGUSR1) now = 1;		
	if (sig == SIGUSR2) show = 1;		
	if (sig == SIGTERM) exit(0);
}


void
cleanup() {
	syslog(LOG_INFO, "Stopping daemon.");
	free(cbreaks);
	free(blacklist);
	fclose(fp);
	xcb_key_symbols_free(xc.symbols);
	xcb_free_gc(xc.c, xc.g);
	xcb_disconnect(xc.c);
	xc.c = NULL;
}



xcb_char2b_t* 
convert_ascii_to_char2b(const char *ascii) {
	size_t i, len;
	xcb_char2b_t *wstr;
	if (ascii == NULL)
		return NULL;
	len = strlen(ascii);
	wstr = malloc(sizeof(xcb_char2b_t) * (len + 1));
	if (wstr == NULL)
		return NULL;
	for (i = 0; i <= len; i++) {
		wstr[i].byte1 = 0;
		wstr[i].byte2 = ascii[i];
	}
	return wstr;
}


uint32_t 
get_ntext_width(const xcb_char2b_t *wstr, size_t n) {
	if (wstr == NULL || n == 0)
		return 0;
	/* Query about text width */
	xcb_query_text_extents_cookie_t cookie = xcb_query_text_extents(xc.c, xc.g, n, wstr);
	xcb_query_text_extents_reply_t *r = xcb_query_text_extents_reply(xc.c, cookie, NULL);
	if (r == NULL) {
		syslog(LOG_WARNING, "Failed to get text information.");
		return 0;
	}
	uint32_t width = r->overall_width;
	free(r);
	return width;
}


uint32_t 
get_ntext8_width(const char *str, size_t n) {
	if (str == NULL || str[0] == 0 || n == 0)
		return 0;
	xcb_char2b_t *wstr = convert_ascii_to_char2b(str);
	if (wstr == NULL)
		return 0;
	uint32_t width = get_ntext_width(wstr, n);
	free(wstr);
	return width;
}


void
create_cb(cbreak *cb) {
	/*pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);*/
	xcb_generic_event_t *e;
	xcb_window_t w;
	char bar[] = "[--------------------------------------------------]"; // 50 symbols + 2 brackets
	uint32_t mask, values[3];
	uint32_t p;

	struct pollfd pollin[1] = {
		{ .fd = xcb_get_file_descriptor(xc.c), .events = POLLIN }
	};

	/* create window */
	mask = XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK;
	values[0] = xc.s->black_pixel;
	values[1] = 1;
	values[2] = XCB_EVENT_MASK_KEY_PRESS;

	w = xcb_generate_id (xc.c);
	xcb_create_window (xc.c, XCB_COPY_FROM_PARENT, w, xc.s->root,
					 0, 0, xc.s->width_in_pixels, xc.s->height_in_pixels, 1,
					 XCB_WINDOW_CLASS_INPUT_OUTPUT,
					 xc.s->root_visual,
					 mask, values);

	xcb_map_window (xc.c, w);
	xcb_set_input_focus(xc.c, XCB_INPUT_FOCUS_FOLLOW_KEYBOARD, w, XCB_CURRENT_TIME);
	xcb_grab_keyboard(xc.c, 0, w, XCB_CURRENT_TIME, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);

	if (flags ^ FLAG_BAR && width_bar == 0) {
		width_bar = get_ntext8_width(bar, strlen(bar));
		bar_x = (xc.s->width_in_pixels / 2) - (width_bar / 2);
		bar_y = xc.s->height_in_pixels / 1.5;
	}

	char timer[6] = "00:00";
	if (flags ^ FLAG_TIMER && width_timer == 0) {
		width_timer = get_ntext8_width(timer, 5);
		timer_x = (xc.s->width_in_pixels / 2) - (width_timer / 2);
		timer_y = xc.s->height_in_pixels / 2;
	}

	double step_for_second = (double)NUMBER_OF_HASH_SYMBOLS / cb->du;
	
	double i = 0; //step_energy
	uint8_t min = 0, sec = 0;
	uint8_t number_of_steps = 0;
	uint32_t now = time(0);

	if (flags & FLAG_BAR == 0) 
		xcb_image_text_8(xc.c, NUMBER_OF_HASH_SYMBOLS + 2, w, xc.g, bar_x, bar_y, bar);
	if (flags & FLAG_TIMER == 0) {
		sprintf(timer,   "%02d", min);
		sprintf(timer+3, "%02d", sec);
		memcpy(timer+2, ":", 1);
		xcb_image_text_8(xc.c, 5, w, xc.g, timer_x, timer_y, timer);
	}
	xcb_flush(xc.c);
	sleep(1);

	for (ever) {
		if (now != time(0)) { // second
			now++;
			sec++;
			if (sec > 59) {
				sec = 0;
				min++;
			}

			i += step_for_second;
			while (i > 1) {
				memcpy(bar + number_of_steps + 1, "#", 1);
				number_of_steps++;
				i--;
			}

			syslog(LOG_DEBUG, "flags %d", flags);
			if (flags & FLAG_BAR == 0) 
				xcb_image_text_8(xc.c, NUMBER_OF_HASH_SYMBOLS + 2, w, xc.g, bar_x, bar_y, bar);
			if (flags & FLAG_TIMER == 0) {
				sprintf(timer,   "%02d", min);
				sprintf(timer+3, "%02d", sec);
				memcpy(timer+2, ":", 1);
				xcb_image_text_8(xc.c, 5, w, xc.g, timer_x, timer_y, timer);
			}
			xcb_flush(xc.c);

			if (min * 60 + sec >= cb->du) {
				if (flags & FLAG_BAR == 0) {
					while (number_of_steps < NUMBER_OF_HASH_SYMBOLS) {
						memcpy(bar + number_of_steps + 1, "#", 1);
						number_of_steps++;
					}
					xcb_image_text_8(xc.c, NUMBER_OF_HASH_SYMBOLS + 2, w, xc.g, bar_x, bar_y, bar);
				}
				xcb_flush(xc.c);
				usleep(100000); // just to show that bar is complete
				xcb_destroy_window(xc.c, w);
				xcb_flush(xc.c);
				cb->rt = time(0) + cb->it;
				return;
				/*pthread_exit(0);*/
			}
		}

		if ((p = poll(pollin, 1, 1000)) > 0) { 
			if (pollin[0].revents & POLLIN) {
				while ((e = xcb_poll_for_event(xc.c))) {
					switch (e->response_type & ~0x80) {
						case XCB_KEY_PRESS: {
							uint32_t col = 0;
							xcb_key_press_event_t *kr = (xcb_key_press_event_t *)e;
							xcb_keysym_t ksym = xcb_key_symbols_get_keysym(xc.symbols, kr->detail, col);
							/*syslog(LOG_DEBUG, "%d\n", ksym);*/
							switch(ksym) {
								// skip
								case XK_s:
									if (flags & FLAG_ENABLE_SKIP) {
										xcb_destroy_window(xc.c, w);
										xcb_flush(xc.c);
										cb->rt = time(0) + cb->it;
										return;
									}
								// postpone
								case XK_p:
									if (flags & FLAG_ENABLE_POSTPONE && cb->pt != 0) {
										xcb_destroy_window(xc.c, w);
										xcb_flush(xc.c);
										cb->rt = time(0) + cb->pt; // add postpone time to remaining time
										return;
									}
								default:
									break;
							}
						}
						break;
					}
					free (e);
				}
			}
		}
	}
}


int 
main (int argc, char **argv) {
	int status;
	char c;
	config_path = calloc(500, 1);
	char *runtime_path_file = calloc(500, 1);
	char *runtime_path_dir;

	runtime_path_dir = getenv(RUNTIME_HOME_ENV);
	strcpy(runtime_path_file, runtime_path_dir);
	strcat(runtime_path_file, PID_FILE);
	if (runtime_path_dir == NULL || ((fp = fopen(runtime_path_file, "a+")) == NULL)) {
		memset(runtime_path_file, '\0', strlen(runtime_path_dir));
		runtime_path_dir = getenv("HOME");
		strcpy(runtime_path_file, runtime_path_dir);
		strcat(runtime_path_file, PID_FILE);
		if (runtime_path_dir == NULL || ((fp = fopen(runtime_path_file, "a+")) == NULL))
			err(1, "Cannot open file %s", runtime_path_file);
	} 
	/*free(runtime_path_file);*/

	pid_t pid = 0;
	if(flock(fileno(fp), LOCK_SH | LOCK_NB)) {
		if (EWOULDBLOCK == errno)
			fread(&pid, 4, 1, fp);
	}
	
	while ((c = getopt (argc, argv, "hvc:nskd")) != -1)
		switch (c) {
			case 'h':
				printf ("%s [-hvnkd] [-c CONFIG_PATH]\n", NAME);
				exit(0);
				break;
			case 'v':
				printf ("%s\n", VERSION);
				exit(0);
				break;
			case 'c':
				snprintf(config_path, 500, "%s", optarg);
				break;
			case 'n':
				if (pid) kill(pid, SIGUSR1);			
				else printf ("Daemon is not running.\n");
				exit(0);
			case 'k':
				if (pid) kill(pid, SIGTERM);
				else printf ("Daemon is not running.\n");
				exit(0);
			case 'd':
				flags |= FLAG_DEBUG;
				break;
			case '?':
				exit(1);
			default:
				abort();
	}

	if (pid) {
		kill(pid, SIGUSR2);
		usleep(20000);
		uint32_t rt = 0;
		fseek(fp, 4, SEEK_SET);
		while (fread(&rt, 1, 4, fp)) {
			rt -= time(0);
			if ((rt / (60 * 60)) > 0) printf ("%02d:", rt / 60 * 60);
			printf ("%02d:%02d\n", rt / 60, rt % 60);
		}
		exit(0);
	}
	else {
		fclose(fp);
		pid_t p = fork();
		switch (p) {
			case -1:
				err(1, "fork error");
			case 0: {
				fp = fopen(runtime_path_file, "w+");
				free(runtime_path_file);
				status = im_daemon();
				exit(status);
			}
			default:
				exit(0);
		}
	}
}
