local ffi = require "ffi"
local C = ffi.C
local min, max = math.min, math.max

-- allow loading modules from within av
-- (use package.preload instead?)
package.path = "./av/?.lua;./av/?/init.lua;" .. package.path

local scheduler = require "scheduler"
local schedule = scheduler.create()
now, go, wait, event = schedule.now, schedule.go, schedule.wait, schedule.event



local function cmd(fmt, ...) 
	local str = string.format(fmt, ...)
	print(str) 
	return io.popen(str):read("*a") 
end

print(ffi.os, ffi.arch)

local libname
if ffi.os == "OSX" then
	libname = "av/libav_core.dylib"
elseif ffi.os == "Linux" then
	libname = "av/libav_core.so"
else
	error("platform not supported")
end

-- load or build
local ok
--ok, core = pcall(ffi.load, libname)
if not ok then 
	print("compile av_core")
	
	local res
	if ffi.os == "OSX" then
		print(cmd("g++ -O3 -fPIC -DEV_MULTIPLICITY=1 -DHAVE_GETTIMEOFDAY -D__MACOSX_CORE__ -I/usr/local/include/luajit-2.0 av/src/av.cpp av/src/RtAudio.cpp /usr/local/lib/libglfw.a -framework Cocoa -framework CoreFoundation -framework IOKit -framework OpenGL -framework CoreAudio -shared -o %s", libname))
		core = ffi.load(libname)
	else
		print(cmd("g++ -O3 -fPIC av/src/av.cpp /usr/local/lib/libglfw.a -shared -o %s", libname))
		core = ffi.load(libname)
	end	
end

--print(io.popen("gcc -E -P av/src/av.h"):read("*a"))
ffi.cdef [[
enum {
	AV_EVENT_TYPE_READ,
	AV_EVENT_TYPE_TIMER,
	AV_EVENT_TYPE_CLOSE,
	
	AV_EVENT_TYPE_COUNT
};

static const int AV_MAXEVENTS = 64;

typedef struct av_event_t {
	int type;
	int fd;
} av_event_t;

typedef struct av_loop_t {
	int queue;					// OSX: kqueue, Linux: epoll
	int numevents;				// how many events were populated by the last poll
	av_event_t * events;		
} av_loop_t;

av_loop_t * av_loop_new();
void av_loop_destroy(av_loop_t * loop);
int av_loop_add_fd_in(av_loop_t * loop, int fd);
int av_loop_add_fd_out(av_loop_t * loop, int fd);
int av_loop_remove_fd_in(av_loop_t * loop, int fd);
int av_loop_remove_fd_out(av_loop_t * loop, int fd);
int av_loop_run_once(av_loop_t * loop, double seconds);

void av_sleep(double seconds);
double av_now();
double av_filetime(const char * filename);

enum {
	AV_AUDIO_CMD_EMPTY,
	AV_AUDIO_CMD_GENERIC = 128,
	AV_AUDIO_CMD_CLEAR,
	AV_AUDIO_CMD_VOICE_ADD,
	AV_AUDIO_CMD_VOICE_REMOVE,
	AV_AUDIO_CMD_VOICE_PARAM,
	AV_AUDIO_CMD_VOICE_CODE,
	
	AV_AUDIO_CMD_SKIP = 255
};

typedef struct av_msg_param {
	int id, pid;
	double value;
} av_msg_param;

typedef struct av_msgbuffer {
	int read, write, size, unused;
	unsigned char * data;
} av_msgbuffer;

typedef struct av_Audio {
	unsigned int blocksize;
	unsigned int frames;	
	unsigned int indevice, outdevice;
	unsigned int inchannels, outchannels;		
	
	double time;		// in seconds
	double samplerate;
	double lag;			// in seconds
	
	av_msgbuffer msgbuffer;
	
	// a big buffer for main-thread audio generation
	float * buffer;
	// the buffer alternates between channels at blocksize periods:
	int blocks, blockread, blockwrite, blockstep;
	
	// only access from audio thread:
	float * input;
	float * output;	
	void (*onframes)(struct av_Audio * self, double sampletime, float * inputs, float * outputs, int frames);
	
} av_Audio;

av_Audio * av_audio_get();
void av_audio_start(); 
]]

local av = {}

setmetatable(av, {
	__index = function(_, k)
		local v = core["av_"..k]
		av[k] = v
		return v
	end
})

local loop = {}
loop.__index = loop
function loop:__gc()
	print("closing loop")
	lib.av_loop_destroy(self)
end

-- call func whenever data is ready to be read on fd
function loop:input(fd, func) 
	if func then
		lib.av_loop_add_fd_in(self, fd)
		readers[fd] = func
	else
		lib.av_loop_remove_fd_in(self, fd)
		readers[fd] = nil
	end
end

ffi.metatype("av_loop_t", loop)

local mainloop = assert(core.av_loop_new(), "failed to create main loop")
--ffi.gc(mainloop, loop.__gc)
local timers = {}
local readers = {}
local writers = {}
local t0 = av.now()
local maxtimercallbacks = 100
local mintimeout = 0.001
local ev = ffi.new("av_event_t[?]", C.AV_MAXEVENTS)

--[[
local function addtimer(t, timer)	
	timer.t = t
	
	-- insertion sort to derive prev & next timers:
	local p, n = nil, timers.head
	while n and n.t < t do
		p = n
		n = n.next
	end
	
	if not p then
		-- n might or might not be nil, either way works:
		timer.next = n
		timers.head = timer
	else
		-- p exists but n might not.
		-- if p exists, timers.head is not changed.
		timer.next = p.next
		p.next = timer
	end
end

-- we could implement go, wait etc. in terms of this:
local function setTimeout(delay, callback)
	--[=[
	local t = now + delay
	local timer = {
		callback = callback,
	}
	addtimer(t, timer)
	--]=]
	go(delay, function()
		local rpt = callback()
		while rpt do
			wait(rpt)
			rpt = callback()
		end
	end)
end
--]]

local function run_once(maxtimeout)
	-- derive our timeout:
	local timeout = maxtimeout or 0.01
	local due = schedule.due()
	if due then
		timeout = max(min(due - now(), timeout), mintimeout)
	end

	-- grab system events:
	local n = core.av_loop_run_once(mainloop, timeout)
	
	-- run scheduled coroutines:
	local t = core.av_now() - t0
	schedule.update(t, maxtimercallbacks)
	
	--[[
	local calls = 0
	while timer and timer.t < t do
		-- advance head before callback
		timers.head = timer.next
		-- change current time before callback:
		now = timer.t
		local rpt = timer.callback()
		if rpt and rpt > 0 then
			-- re-insert:
			addtimer(now + rpt, timer)
		end
		-- repeat
		timer = timers.head
		-- TODO: break at maxtimers?
		calls = calls + 1
		if calls > maxtimercallbacks then
			print("warning: aborting timers (suspected feedback loop)")
			break
		end
	end
	-- now update to real time
	now = t
		
	-- now handle IO:
	for i = 0, n-1 do
		local ty = ev[i].type
		local id = ev[i].fd
		--print("event", ty, id)
		if ty == C.AV_EVENT_TYPE_READ then
			--print("read", id)
			local f = readers[id]
			if f then
				f(id)
			else
				local data = readbytes(fd)
				print("no reader", id, data)
			end
		elseif ty == C.AV_EVENT_TYPE_TIMER then
			local f = timers[id]
			if f then
				f(id)
			else
				print("no timer", id)
			end
		elseif ty == C.AV_EVENT_TYPE_CLOSE then
			--print("closed", id)
		else
			print("unhandled event type")
		end
	end
	--]]
	-- handle system events:
	for i = 0, n-1 do
		local ev = mainloop.events[i]
		local ty = ev.type
		local id = ev.fd
		--print("event", ty, id)
		if ty == core.AV_EVENT_TYPE_READ then
			print("read", id)
			local f = readers[id]
			if f then
				f(id)
			else
				local data = readbytes(fd)
				print("no reader", id, data)
			end
		elseif ty == core.AV_EVENT_TYPE_WRITE then
			print("write", id)
			local f = writers[id]
			if f then
				f(id)
			else
				print("no writer", id)
			end
		elseif ty == core.AV_EVENT_TYPE_CLOSE then
			print("closed", id)
			error()
		else
			print("unhandled event type")
		end
	end
end	

local window = require "window"

function av.run()
	while window.running do
		run_once(0.01)
	end
end

return av