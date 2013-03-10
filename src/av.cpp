
#if defined(_WIN32) || defined(__WINDOWS_MM__) || defined(_WIN64)
	#define AV_WINDOWS 1
	// just placeholder really; Windows requires a bit more work yet.
	#include <windows.h>
	#include <direct.h>
	
	#include <gl\gl.h> 
	#include <gl\glu.h> 
	#include <glut.h>
	
	
	#define AV_PATH_MAX MAX_PATH
	#define AV_GETCWD _getcwd
	#define AV_SNPRINTF _snprintf
#else
	// Unixen:
	#include <unistd.h>
	#include <sys/time.h>
	#include <time.h>
	#include <libgen.h>
	#if defined( __APPLE__ ) && defined( __MACH__ )
		#define AV_OSX 1
		#include <OpenGL/OpenGL.h>
		#include <GLUT/glut.h>

	#else
		#define AV_LINUX 1
		#include <GL/gl.h>
		#include <GL/glut.h>
		
	#endif
	
	#define AV_PATH_MAX PATH_MAX
	#define AV_GETCWD getcwd
	#define AV_SNPRINTF snprintf
#endif

#include <stdlib.h>
#include <stdio.h>

extern "C" {
	#include "lua.h"
	#include "lualib.h"
	#include "lauxlib.h"

	int luaopen_builtin(lua_State * L);
}

#include "av.h"

/*
// the path from where it was invoked:
char launchpath[AV_PATH_MAX+1];
// the path where the binary actually resides, e.g. with modules:
char apppath[AV_PATH_MAX+1];
// the path of the start file, e.g. user script / workspace:
char workpath[AV_PATH_MAX+1];
// filename of the main script:
char mainfile[AV_PATH_MAX+1];

void getpaths(int argc, char ** argv) {
	char wd[AV_PATH_MAX];
	if (AV_GETCWD(wd, AV_PATH_MAX) == 0) {
		printf("could not derive working path\n");
		exit(0);
	}
	
	// get binary path:
	char tmppath[AV_PATH_MAX];
	#ifdef AV_OSX
		AV_SNPRINTF(launchpath, AV_PATH_MAX, "%s/", wd);
		#ifdef AV_OSXAPP
			// launched as a .app:
			AV_SNPRINTF(apppath, AV_PATH_MAX, "%s", launchpath);
		#else
			// launched as a console app:
			if (argc > 0) {
				realpath(argv[0], tmppath);
			}
			AV_SNPRINTF(apppath, AV_PATH_MAX, "%s/", dirname(tmppath));
		#endif
		
		
	#elif defined(AV_WINDOWS)
		// Windows only:
		{
		_splitpath(wd, NULL, wd, NULL, NULL);
		AV_SNPRINTF(launchpath, AV_PATH_MAX, "%s", wd);
		DWORD retval = GetFullPathName(argv[0],
                 AV_PATH_MAX,
                 tmppath,
                 NULL);
		_splitpath(tmppath, NULL, tmppath, NULL, NULL);
		AV_SNPRINTF(apppath, AV_PATH_MAX, "%s", (tmppath));
		}
	#else
		AV_SNPRINTF(launchpath, AV_PATH_MAX, "%s/", wd);
		// Linux only?
		int count = readlink("/proc/self/exe", tmppath, AV_PATH_MAX);
		if (count > 0) {
			tmppath[count] = '\0';
		} else if (argc > 0) {
			realpath(argv[0], tmppath);
		}
		AV_SNPRINTF(apppath, AV_PATH_MAX, "%s/", dirname(tmppath));
	#endif
	
	char apath[AV_PATH_MAX];
	#if defined(AV_WINDOWS)
		{
		DWORD retval = GetFullPathName(argv[1],
                 AV_PATH_MAX,
                 tmppath,
                 NULL);
		_splitpath(tmppath, NULL, workpath, mainfile, NULL);
		AV_SNPRINTF(apppath, AV_PATH_MAX, "%s", (tmppath));
		}
	#else
	if (argc > 1) {
		realpath(argv[1], apath);
		
		AV_SNPRINTF(mainfile, AV_PATH_MAX, "%s", basename(apath));
		AV_SNPRINTF(workpath, AV_PATH_MAX, "%s/", dirname(apath));
	} else {
		// just copy the current path:
		AV_SNPRINTF(workpath, AV_PATH_MAX, "%s", launchpath);
		AV_SNPRINTF(mainfile, AV_PATH_MAX, "%s", "main.lua");
	}
	#endif
	
	printf("launchpath %s\n", launchpath);
	printf("apppath %s\n", apppath);
	printf("workpath %s\n", workpath);
	printf("mainfile %s\n", mainfile);
}
*/

// implement av_Window using GLUT:
struct av_Window_GLUT : public av_Window {
	
	// GLUT specific:
	int id;
	int non_fullscreen_width, non_fullscreen_height;
	bool reload;
	
	av_Window_GLUT() {
		width = 720;
		height = 480;
		is_fullscreen = 0;
		reset();
	}
	
	void reset() {
		shift = alt = ctrl = 0;
		fps = 60;
		oncreate = 0;
		onresize = 0;
		onvisible = 0;
		ondraw = 0;
		onkey = 0;
		onmouse = 0;
		id = 0;
		non_fullscreen_width = width;
		non_fullscreen_height = height;
		reload = true;
	}
};


// the window
av_Window_GLUT win;

void timerfunc(int id) {

	// TODO: trigger scheduled events... 
	//av_tick();
	
	// update window:
	if (win.reload && win.oncreate) {
		(win.oncreate)(&win);
		win.reload = false;
	}
	
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	// ortho2d here?
	
	if (win.ondraw) {
		(win.ondraw)(&win);
	}	
	glutSwapBuffers();
	
	// reschedule:
	glutTimerFunc((unsigned int)(1000.0/win.fps), timerfunc, 0);
}

void av_window_settitle(av_Window * self, const char * name) {
	glutSetWindowTitle(name);
}

void av_window_setfullscreen(av_Window * self, int b) {
	win.reload = true;
	win.is_fullscreen = b;
	if (b) {
		glutFullScreen();
	} else {
		glutReshapeWindow(win.non_fullscreen_width, win.non_fullscreen_height);
	}
}


void av_window_setdim(av_Window * self, int x, int y) {
	glutReshapeWindow(x, y);
}

av_Window * av_window_create() {
	return &win;
}

void av_state_reset(void * self) {
	win.reset();
}

void getmodifiers() {
	int mod = glutGetModifiers();
	win.shift = mod & GLUT_ACTIVE_SHIFT;
	win.alt = mod & GLUT_ACTIVE_ALT;
	win.ctrl = mod & GLUT_ACTIVE_CTRL;
}

void onkeydown(unsigned char k, int x, int y) {
	getmodifiers();
	if (win.onkey) {
		(win.onkey)(&win, 1, k);
	}
}

void onkeyup(unsigned char k, int x, int y) {
	getmodifiers();
	if (win.onkey) {
		(win.onkey)(&win, 2, k);
	}
}

void onspecialkeydown(int key, int x, int y) {
	getmodifiers();
	
	#define CS(k) case GLUT_KEY_##k: key = AV_KEY_##k; break;
	switch(key){
		CS(LEFT) CS(UP) CS(RIGHT) CS(DOWN)
		CS(PAGE_UP) CS(PAGE_DOWN)
		CS(HOME) CS(END) CS(INSERT)

		CS(F1) CS(F2) CS(F3) CS(F4)
		CS(F5) CS(F6) CS(F7) CS(F8)
		CS(F9) CS(F10)	CS(F11) CS(F12)
	}
	#undef CS
	
	if (win.onkey) {
		(win.onkey)(&win, 1, key);
	}
}

void onspecialkeyup(int key, int x, int y) {
	getmodifiers();
	
	#define CS(k) case GLUT_KEY_##k: key = AV_KEY_##k; break;
	switch(key){
		CS(F1) CS(F2) CS(F3) CS(F4)
		CS(F5) CS(F6) CS(F7) CS(F8)
		CS(F9) CS(F10)	CS(F11) CS(F12)
		
		CS(LEFT) CS(UP) CS(RIGHT) CS(DOWN)
		CS(PAGE_UP) CS(PAGE_DOWN)
		CS(HOME) CS(END) CS(INSERT)
	}
	#undef CS
	
	if (win.onkey) {
		(win.onkey)(&win, 2, key);
	}
}

void onmouse(int button, int state, int x, int y) {
	getmodifiers();
	win.button = button;
	if (win.onmouse) {
		(win.onmouse)(&win, state, win.button, x, y);
	}
}

void onmotion(int x, int y) {
	if (win.onmouse) {
		(win.onmouse)(&win, 2, win.button, x, y);
	}
}

void onpassivemotion(int x, int y) {
	if (win.onmouse) {
		(win.onmouse)(&win, 3, win.button, x, y);
	}
}

void onvisibility(int state) {
	if (win.onvisible) (win.onvisible)(&win, state);
}

void ondisplay() {}
void onreshape(int w, int h) {
	win.width = w;
	win.height = h;
	if (!win.is_fullscreen) {
		win.non_fullscreen_width = win.width;
		win.non_fullscreen_height = win.height;
	}
	if (win.onresize) {
		(win.onresize)(&win, w, h);
	}
}

#ifdef AV_WINDOWS
	#include < time.h >
	#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
	  #define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64
	#else
	  #define DELTA_EPOCH_IN_MICROSECS  11644473600000000ULL
	#endif
	 
	struct timezone 
	{
	  int  tz_minuteswest; /* minutes W of Greenwich */
	  int  tz_dsttime;     /* type of dst correction */
	};
	 
	int gettimeofday(struct timeval *tv, struct timezone *tz)
	{
	  FILETIME ft;
	  unsigned __int64 tmpres = 0;
	  static int tzflag;
	 
	  if (NULL != tv)
	  {
		GetSystemTimeAsFileTime(&ft);
	 
		tmpres |= ft.dwHighDateTime;
		tmpres <<= 32;
		tmpres |= ft.dwLowDateTime;
	 
		/*converting file time to unix epoch*/
		tmpres -= DELTA_EPOCH_IN_MICROSECS; 
		tmpres /= 10;  /*convert into microseconds*/
		tv->tv_sec = (long)(tmpres / 1000000UL);
		tv->tv_usec = (long)(tmpres % 1000000UL);
	  }
	 
	  if (NULL != tz)
	  {
		if (!tzflag)
		{
		  _tzset();
		  tzflag++;
		}
		tz->tz_minuteswest = _timezone / 60;
		tz->tz_dsttime = _daylight;
	  }
	 
	  return 0;
	}
#endif

double av_time() {
		timeval t;
		gettimeofday(&t, NULL);
		return (double)t.tv_sec + (((double)t.tv_usec) * 1.0e-6);
}	

void av_sleep(double seconds) {
	#ifdef AV_WINDOWS
		Sleep((DWORD)(seconds * 1.0e3));
	#else
		time_t sec = (time_t)seconds;
		long long int nsec = 1.0e9 * (seconds - (double)sec);
		timespec tspec = { sec, nsec };
		while (nanosleep(&tspec, &tspec) == -1) {
			continue;
		}
	#endif
}

#include "av_ffi_header.cpp"

int luaopen_builtin(lua_State * L) {

	static struct luaL_reg lib[] = {
		// { "name", func },
		{ NULL, NULL },
	};
	luaL_register(L, "builtin", lib);
	
	luaL_loadstring(L, av_ffi_header);
	lua_pcall(L, 0, 1, 0);
	lua_setfield(L, -2, "header");
	
	return 1;
}


int main(int argc, char * argv[]) {


	// initialize paths:
	// getpaths(argc, argv);	
	
	// On Windows, how can we get the exe path when double-clicked?
	// On OSX we can do equivalent of cd $(dirname $0) to go to current path
	// whether launched from terminal or double-clicked
	// does that work on Linux?
	// next stage will be drag & drop startfile onto app...
	char startpath[AV_PATH_MAX];
	#ifdef AV_WINDOWS
		if (AV_GETCWD(startpath, AV_PATH_MAX) == 0) {
			printf("could not derive working path\n");
			exit(0);
		}
	#else
		// grab it from argv[0] (application name)
		AV_SNPRINTF(startpath, AV_PATH_MAX, "%s", dirname(argv[0]));
		#ifdef AV_OSXAPP
			// if launched as app, then the path has /av.app/Contents/MacOS prefixed
			// (which needs to be removed)
			char tmp[AV_PATH_MAX];
			AV_SNPRINTF(tmp, AV_PATH_MAX, "%s/../../../", startpath);
			realpath(tmp, startpath);		
		#endif
	#endif
	
	// use this as the current working directory from now on:
	//printf("running from %s\n", startpath);
	chdir(startpath);
	
	
	// configure GLUT:
	glutInit(&argc, argv);
	
//	screen_width = glutGet(GLUT_SCREEN_WIDTH);
//	screen_height = glutGet(GLUT_SCREEN_HEIGHT);	
	
	glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
	glutInitWindowSize(win.width, win.height);
	glutInitWindowPosition(0, 0);
	
	win.id = glutCreateWindow("");	// << FAIL?
	//printf("initializing window\n");
	glutSetWindow(win.id);
	
	
//	glutIgnoreKeyRepeat(1);
//	glutSetCursor(GLUT_CURSOR_NONE);

	glutKeyboardFunc(onkeydown);
	glutKeyboardUpFunc(onkeyup);
	glutMouseFunc(onmouse);
	glutMotionFunc(onmotion);
	glutPassiveMotionFunc(onpassivemotion);
	glutSpecialFunc(onspecialkeydown);
	glutSpecialUpFunc(onspecialkeyup);
	glutVisibilityFunc(onvisibility);
	glutReshapeFunc(onreshape);
	glutDisplayFunc(ondisplay);
	
	lua_State * L = lua_open();
	luaL_openlibs(L);

	lua_getglobal(L, "package");
	lua_getfield(L, -1, "preload");
		lua_pushcfunction(L, luaopen_builtin);
		lua_setfield(L, -2, "builtin");
	lua_pop(L, 2);
	
	lua_getglobal(L, "debug");
	lua_pushliteral(L, "traceback");
	lua_gettable(L, -2);
	lua_setfield(L, LUA_REGISTRYINDEX, "debug.traceback");
	
	lua_settop(L, 0); // clean stack
		
	const char * startfile = argc > 1 ? argv[1] : "./start.lua";
	lua_getfield(L, LUA_REGISTRYINDEX, "debug.traceback");
	int debugtraceback = lua_gettop(L);
	
	int err = luaL_loadstring(L, av_main);
	//int err = luaL_loadfile(L, startfile);
	if (err == 0) {
		for (int i=0; i<argc; i++) {
			lua_pushstring(L, argv[i]);
		}
		err = lua_pcall(L, argc, LUA_MULTRET, debugtraceback);
	}
	if (err) {
		printf("error (%s): %s\n", startfile, lua_tostring(L, -1));
		return 0;
	}
	
	// start it up:
	glutTimerFunc((unsigned int)(1000.0/win.fps), timerfunc, 0);
	//atexit(terminate);
	glutMainLoop();
	
	lua_close(L);
	
	return 0;
}