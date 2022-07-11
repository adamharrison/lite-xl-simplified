
#if !DIRMONITOR_BACKEND_WIN32 && !DIRMONITOR_BACKEND_INOTIFY && !DIRMONITOR_BACKEND_KQUEUE && !DIRMONTOR_BACKEND_DUMMY
  #ifdef _WIN32
    #define DIRMONITOR_BACKEND_WIN32 1
  #elif __linux__
    #define DIRMONITOR_BACKEND_INOTIFY 1
  #elif __APPLE__ || __FreeBSD__
    #define DIRMONITOR_BACKEND_KQUEUE 1
  #else
    #define DIRMONTOR_BACKEND_DUMMY 1
  #endif
#endif
#include "api.h"
#include <SDL.h>
#include <stdlib.h>
#ifdef DIRMONITOR_BACKEND_WIN32
  #include <windows.h>
#elif DIRMONITOR_BACKEND_INOTIFY
  #include <sys/inotify.h>
  #include <stdlib.h>
  #include <fcntl.h>
  #include <poll.h>
#elif DIRMONITOR_BACKEND_KQUEUE
  #include <sys/event.h>
#endif
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

static unsigned int DIR_EVENT_TYPE = 0;

struct dirmonitor {
  SDL_Thread* thread;
  SDL_mutex* mutex;
  char buffer[64512];
  volatile int length;
  #if DIRMONITOR_BACKEND_INOTIFY || DIRMONITOR_BACKEND_KQUEUE
    int fd;
  #endif
  #if DIRMONITOR_BACKEND_INOTIFY
    // a pipe is used to wake the thread in case of exit
    int sig[2];
  #elif DIRMONITOR_BACKEND_WIN32
    HANDLE handle;
  #endif
};

#if DIRMONITOR_BACKEND_WIN32
  static void close_monitor_handle(struct dirmonitor* monitor) {
    if (monitor->handle && monitor->handle != INVALID_HANDLE_VALUE) {
      HANDLE handle = monitor->handle;
      monitor->handle = NULL;
      CancelIoEx(handle, NULL);
      CloseHandle(handle);
    }
  }
#endif

int get_changes_dirmonitor(struct dirmonitor* monitor, char* buffer, int buffer_size) {
  #if DIRMONITOR_BACKEND_INOTIFY
    struct pollfd fds[2] = { { .fd = monitor->fd, .events = POLLIN | POLLERR, .revents = 0 }, { .fd = monitor->sig[0], .events = POLLIN | POLLERR, .revents = 0 } };
    poll(fds, 2, -1);
    return read(monitor->fd, buffer, buffer_size);
  #elif DIRMONITOR_BACKEND_WIN32
    HANDLE handle = monitor->handle;
    if (handle && handle != INVALID_HANDLE_VALUE) {
      DWORD bytes_transferred;
      if (ReadDirectoryChangesW(handle, buffer, buffer_size, TRUE,  FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME, &bytes_transferred, NULL, NULL) == 0)
        return 0;
      return bytes_transferred;
    }
    return 0;
  #elif DIRMONITOR_BACKEND_KQUEUE
    int nev = kevent(monitor->fd, NULL, 0, (struct kevent*)buffer, buffer_size / sizeof(kevent), NULL);
    if (nev == -1)
      return -1;
    if (nev <= 0)
      return 0;
    return nev * sizeof(struct kevent);
  #else
    return -1;
  #endif
}


int translate_changes_dirmonitor(struct dirmonitor* monitor, char* buffer, int buffer_size, int (*change_callback)(int, const char*, void*), void* data) {
  #if DIRMONITOR_BACKEND_INOTIFY
    for (struct inotify_event* info = (struct inotify_event*)buffer; (char*)info < buffer + buffer_size; info = (struct inotify_event*)((char*)info + sizeof(struct inotify_event)))
      change_callback(info->wd, NULL, data);
  #elif DIRMONITOR_BACKEND_WIN32
    for (FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)buffer; (char*)info < buffer + buffer_size; info = (FILE_NOTIFY_INFORMATION*)(((char*)info) + info->NextEntryOffset)) {
      char transform_buffer[PATH_MAX*4];
      int count = WideCharToMultiByte(CP_UTF8, 0, (WCHAR*)info->FileName, info->FileNameLength, transform_buffer, PATH_MAX*4 - 1, NULL, NULL);
      change_callback(count, transform_buffer, data);
      if (!info->NextEntryOffset)
        break;
    }
  #elif DIRMONITOR_BACKEND_INOTIFY
    for (struct kevent* info = (struct kevent*)buffer; (char*)info < buffer + buffer_size; info = (struct kevent*)(((char*)info) + sizeof(kevent)))
      change_callback(info->ident, NULL, data);
  #endif
  return 0;
}


int add_dirmonitor(struct dirmonitor* monitor, const char* path) {
  #if DIRMONITOR_BACKEND_INOTIFY
    return inotify_add_watch(monitor->fd, path, IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MODIFY | IN_MOVED_TO);
  #elif DIRMONITOR_BACKEND_WIN32
    close_monitor_handle(monitor);
    monitor->handle = CreateFileA(path, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    return !monitor->handle || monitor->handle == INVALID_HANDLE_VALUE ? -1 : 1;
  #elif DIRMONITOR_BACKEND_KQUEUE
    int fd = open(path, O_RDONLY);
    struct kevent change;
    EV_SET(&change, fd, EVFILT_VNODE, EV_ADD | EV_CLEAR, NOTE_DELETE | NOTE_EXTEND | NOTE_WRITE | NOTE_ATTRIB | NOTE_LINK | NOTE_RENAME, 0, (void*)path);
    kevent(monitor->fd, &change, 1, NULL, 0, NULL);
    return fd;
  #else
    return -1;
  #endif
}


void remove_dirmonitor(struct dirmonitor* monitor, int fd) {
  #if DIRMONITOR_BACKEND_INOTIFY
    inotify_rm_watch(monitor->fd, fd);
  #elif DIRMONITOR_BACKEND_WIN32
    close_monitor_handle(monitor);
  #elif DIRMONITOR_BACKEND_KQUEUE
    close(fd);
  #endif
}


static int f_check_dir_callback(int watch_id, const char* path, void* L) {
  lua_pushvalue(L, -1);
  if (path)
    lua_pushlstring(L, path, watch_id);
  else
    lua_pushnumber(L, watch_id);
  lua_call(L, 1, 1);
  int result = lua_toboolean(L, -1);
  lua_pop(L, 1);
  return !result;
}


static int dirmonitor_check_thread(void* data) {
  struct dirmonitor* monitor = data;
  while (monitor->length >= 0) {
    if (monitor->length == 0) {
      int result = get_changes_dirmonitor(monitor, monitor->buffer, sizeof(monitor->buffer));
      SDL_LockMutex(monitor->mutex);
      if (monitor->length == 0)
        monitor->length = result;
      SDL_UnlockMutex(monitor->mutex);
    }
    SDL_Delay(1);
    SDL_Event event = { .type = DIR_EVENT_TYPE };
    SDL_PushEvent(&event);
  }
  return 0;
}


static int f_dirmonitor_new(lua_State* L) {
  if (DIR_EVENT_TYPE == 0)
    DIR_EVENT_TYPE = SDL_RegisterEvents(1);
  struct dirmonitor* monitor = lua_newuserdata(L, sizeof(struct dirmonitor));
  luaL_setmetatable(L, API_TYPE_DIRMONITOR);
  memset(monitor, 0, sizeof(struct dirmonitor));
  #if DIRMONITOR_BACKEND_INOTIFY
    monitor->fd = inotify_init();
    (void)!pipe(monitor->sig);
    fcntl(monitor->sig[0], F_SETFD, FD_CLOEXEC);
    fcntl(monitor->sig[1], F_SETFD, FD_CLOEXEC);
  #elif DIRMONITOR_BACKEND_KQUEUE
    monitor->fd = kqueue();
  #endif
  return 1;
}


static int f_dirmonitor_gc(lua_State* L) {
  struct dirmonitor* monitor = luaL_checkudata(L, 1, API_TYPE_DIRMONITOR);
  SDL_LockMutex(monitor->mutex);
  monitor->length = -1;
  #if DIRMONITOR_BACKEND_INOTIFY || DIRMONITOR_BACKEND_KQUEUE
    close(monitor->fd);
  #endif
  #if DIRMONITOR_BACKEND_INOTIFY
    close(monitor->sig[0]);
    close(monitor->sig[1]);
  #elif DIRMONITOR_BACKEND_WIN32
    close_monitor_handle(monitor);
  #endif
  SDL_UnlockMutex(monitor->mutex);
  SDL_WaitThread(monitor->thread, NULL);
  SDL_DestroyMutex(monitor->mutex);
  return 0;
}


static int f_dirmonitor_watch(lua_State *L) {
  struct dirmonitor* monitor = luaL_checkudata(L, 1, API_TYPE_DIRMONITOR);
  lua_pushnumber(L, add_dirmonitor(monitor, luaL_checkstring(L, 2)));
  #if !DIRMONITOR_BACKEND_DUMMY
  if (!monitor->thread)
    monitor->thread = SDL_CreateThread(dirmonitor_check_thread, "dirmonitor_check_thread", monitor);
  #endif
  lua_pushnumber(L, add_dirmonitor(monitor, luaL_checkstring(L, 2)));
  return 1;
}


static int f_dirmonitor_unwatch(lua_State *L) {
  remove_dirmonitor(((struct dirmonitor*)luaL_checkudata(L, 1, API_TYPE_DIRMONITOR)), lua_tonumber(L, 2));
  return 0;
}


static int f_dirmonitor_check(lua_State* L) {
  struct dirmonitor* monitor = luaL_checkudata(L, 1, API_TYPE_DIRMONITOR);
  SDL_LockMutex(monitor->mutex);
  if (monitor->length < 0)
    lua_pushnil(L);
  else if (monitor->length > 0) {
    if (translate_changes_dirmonitor(monitor, monitor->buffer, monitor->length, f_check_dir_callback, L) == 0)
      monitor->length = 0;
    lua_pushboolean(L, 1);
  } else
    lua_pushboolean(L, 0);
  SDL_UnlockMutex(monitor->mutex);
  return 1;
}



static const luaL_Reg dirmonitor_lib[] = {
  { "new",      f_dirmonitor_new         },
  { "__gc",     f_dirmonitor_gc          },
  { "watch",    f_dirmonitor_watch       },
  { "unwatch",  f_dirmonitor_unwatch     },
  { "check",    f_dirmonitor_check       },
  {NULL, NULL}
};


int luaopen_dirmonitor(lua_State* L) {
  luaL_newmetatable(L, API_TYPE_DIRMONITOR);
  luaL_setfuncs(L, dirmonitor_lib, 0);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  return 1;
}
