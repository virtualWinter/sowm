// sowm - An itsy bitsy floating window manager.

#include <X11/Xlib.h>
#include <X11/XF86keysym.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/Xinerama.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#include "sowm.h"

static client       *list = {0}, *ws_list[10] = {0}, *cur;
static int          ws = 1, sw, sh, wx, wy, numlock = 0, monitors;
static unsigned int ww, wh;

static int          s;
static Display      *d;
static XButtonEvent mouse;
static Window       root;

static void (*events[LASTEvent])(XEvent *e) = {
    [ButtonPress]      = button_press,
    [ButtonRelease]    = button_release,
    [ConfigureRequest] = configure_request,
    [KeyPress]         = key_press,
    [MapRequest]       = map_request,
    [MappingNotify]    = mapping_notify,
    [DestroyNotify]    = notify_destroy,
    [EnterNotify]      = notify_enter,
    [MotionNotify]     = notify_motion
};

#include "config.h"

unsigned long getcolor(const char *col) {
    Colormap m = DefaultColormap(d, s);
    XColor c;
    return (!XAllocNamedColor(d, m, col, &c, &c))?0:c.pixel;
}

void win_focus(client *c) {
    if (cur) XSetWindowBorder(d, cur->w, getcolor(BORDER_NORMAL));
    cur = c;

    XSetWindowBorder(d, cur->w, getcolor(BORDER_SELECT));

    if (cur->fs) {
        XConfigureWindow(d, cur->w, CWBorderWidth, &(XWindowChanges){.border_width = 0});
    } else {
        XConfigureWindow(d, cur->w, CWBorderWidth, &(XWindowChanges){.border_width = BORDER_WIDTH});
    }
    XSetInputFocus(d, cur->w, RevertToParent, CurrentTime);
}

void notify_destroy(XEvent *e) {
    win_del(e->xdestroywindow.window);

    if (list) win_focus(list->prev);
}

void notify_enter(XEvent *e) {
    while(XCheckTypedEvent(d, EnterNotify, e));
    while(XCheckTypedWindowEvent(d, mouse.subwindow, MotionNotify, e));

    for win if (c->w == e->xcrossing.window) win_focus(c);
}

void notify_motion(XEvent *e) {
    if (!mouse.subwindow || cur->f) return;

    while(XCheckTypedEvent(d, MotionNotify, e));

    int xd = e->xbutton.x_root - mouse.x_root;
    int yd = e->xbutton.y_root - mouse.y_root;

    XMoveResizeWindow(d, mouse.subwindow,
        wx + (mouse.button == 1 ? xd : 0),
        wy + (mouse.button == 1 ? yd : 0),
        MAX(1, ww + (mouse.button == 3 ? xd : 0)),
        MAX(1, wh + (mouse.button == 3 ? yd : 0)));

    win_size(cur->w, &cur->wx, &cur->wy, &cur->ww, &cur->wh);
}

void key_press(XEvent *e) {
    KeySym keysym = XkbKeycodeToKeysym(d, e->xkey.keycode, 0, 0);

    for (unsigned int i=0; i < sizeof(keys)/sizeof(*keys); ++i)
        if (keys[i].keysym == keysym &&
            mod_clean(keys[i].mod) == mod_clean(e->xkey.state))
            keys[i].function(keys[i].arg);
}

void button_press(XEvent *e) {
    if (!e->xbutton.subwindow) return;

    /* Click-to-focus: focus and raise the window under the cursor. */
    for win if (c->w == e->xbutton.subwindow) win_focus(c);
    XRaiseWindow(d, e->xbutton.subwindow);

    /* Only MOD+click starts a move/resize. */
    if (mod_clean(e->xbutton.state) != MOD) return;

    win_size(e->xbutton.subwindow, &wx, &wy, &ww, &wh);
    mouse = e->xbutton;
}

void button_release(XEvent *e) {
    mouse.subwindow = 0;
}

void win_add(Window w) {
    client *c;

    if (!(c = (client *) calloc(1, sizeof(client))))
        exit(1);

    c->w = w;

    if (list) {
        list->prev->next = c;
        c->prev          = list->prev;
        list->prev       = c;
        c->next          = list;

    } else {
        list = c;
        list->prev = list->next = list;
    }

    ws_save(ws);
    win_focus(c);
}

void win_del(Window w) {
    client *x = 0;

    for win if (c->w == w) x = c;

    if (!list || !x)  return;
    if (x->prev == x) list = cur = 0;
    if (list == x)    list = x->next;
    if (x->next)      x->next->prev = x->prev;
    if (x->prev)      x->prev->next = x->next;

    free(x);
    ws_save(ws);
}

void win_kill(const Arg arg) {
    if (cur) XKillClient(d, cur->w);
}

int multimonitor_action(int action) { // action = 0 -> center; action = 1 -> fs
    if (!XineramaIsActive(d)) return 1;

    XineramaScreenInfo *si = XineramaQueryScreens(d, &monitors);
    int ret = 1;

    for (int i = 0; i < monitors; i++) {
        if ((cur->wx + (cur->ww / 2) >= (unsigned int)si[i].x_org
                && cur->wx + (cur->ww / 2) < (unsigned int)si[i].x_org + si[i].width)
            && (cur->wy + (cur->wh / 2) >= (unsigned int)si[i].y_org
                && cur->wy + (cur->wh / 2) < (unsigned int)si[i].y_org + si[i].height)) {

            if (action)
                XMoveResizeWindow(d, cur->w,
                                  si[i].x_org, si[i].y_org,
                                  si[i].width, si[i].height);
            else
                XMoveWindow(d, cur->w,
                            si[i].x_org + ((si[i].width - ww) / 2),
                            si[i].y_org + ((si[i].height - wh) / 2));
            ret = 0;
            break;
        }
    }

    XFree(si);
    return ret;
}

void win_center(const Arg arg) {
    if (!cur) return;

    win_size(cur->w, &(int){0}, &(int){0}, &ww, &wh);

    if (multimonitor_action(0)) {
        XMoveWindow(d, cur->w, (sw - ww) / 2, (sh - wh) / 2);
    }

    win_size(cur->w, &cur->wx, &cur->wy, &cur->ww, &cur->wh);
}

void win_fs(const Arg arg) {
    if (!cur) return;

    if ((cur->f = cur->f ? 0 : 1)) {
        win_size(cur->w, &cur->wx, &cur->wy, &cur->ww, &cur->wh);

        if (multimonitor_action(1)) {
            XMoveResizeWindow(d, cur->w, 0, GAP_SIZE, sw, sh - GAP_SIZE);
        }

        cur->fs = 1;
        win_focus(cur);

    } else {
        XMoveResizeWindow(d, cur->w, cur->wx, cur->wy, cur->ww, cur->wh);
        cur->fs = 0;
        win_focus(cur);
    }
}

void win_to_ws(const Arg arg) {
    int tmp = ws;

    if (arg.i == tmp) return;

    ws_sel(arg.i);
    win_add(cur->w);
    ws_save(arg.i);

    ws_sel(tmp);
    XUnmapWindow(d, cur->w);
    win_del(cur->w);
    ws_save(tmp);

    if (list) win_focus(list);
}

void win_prev(const Arg arg) {
    if (!cur) return;

    XRaiseWindow(d, cur->prev->w);
    win_focus(cur->prev);
}

void win_next(const Arg arg) {
    if (!cur) return;

    XRaiseWindow(d, cur->next->w);
    win_focus(cur->next);
}

void ws_go(const Arg arg) {
    int tmp = ws;

    if (arg.i == ws) return;

    ws_save(ws);
    ws_sel(arg.i);

    for win XMapWindow(d, c->w);

    ws_sel(tmp);

    for win {
        char* winame = NULL;
        if (!XFetchName(d, c->w, &winame) || winame == NULL) {
            XUnmapWindow(d, c->w);
        } else {
            if (strncmp(winame, barname, strlen(barname))) {
                XUnmapWindow(d, c->w);
            }
            XFree(winame);
        }
    }

    ws_sel(arg.i);

    if (list) win_focus(list); else cur = 0;
}

void configure_request(XEvent *e) {
    XConfigureRequestEvent *ev = &e->xconfigurerequest;

    XConfigureWindow(d, ev->window, ev->value_mask, &(XWindowChanges) {
        .x          = ev->x,
        .y          = ev->y,
        .width      = ev->width,
        .height     = ev->height,
        .sibling    = ev->above,
        .stack_mode = ev->detail
    });
}

void map_request(XEvent *e) {
    Window w = e->xmaprequest.window;

    XSelectInput(d, w, StructureNotifyMask|EnterWindowMask);
    win_size(w, &wx, &wy, &ww, &wh);
    win_add(w);
    cur = list->prev;

    if (wx + wy == 0) win_center((Arg){0});

    XMapWindow(d, w);
    win_focus(list->prev);
}

void mapping_notify(XEvent *e) {
    XMappingEvent *ev = &e->xmapping;

    if (ev->request == MappingKeyboard || ev->request == MappingModifier) {
        XRefreshKeyboardMapping(ev);
        input_grab(root);
    }
}

void run(const Arg arg) {
    if (fork()) return;
    if (d) close(ConnectionNumber(d));

    setsid();
    execvp((char*)arg.com[0], (char**)arg.com);
}

void input_grab(Window root) {
    unsigned int i, j, modifiers[] = {0, LockMask, numlock, numlock|LockMask};
    XModifierKeymap *modmap = XGetModifierMapping(d);
    KeyCode code;

    for (i = 0; i < 8; i++)
        for (int k = 0; k < modmap->max_keypermod; k++)
            if (modmap->modifiermap[i * modmap->max_keypermod + k]
                == XKeysymToKeycode(d, 0xff7f))
                numlock = (1 << i);

    XUngrabKey(d, AnyKey, AnyModifier, root);

    for (i = 0; i < sizeof(keys)/sizeof(*keys); i++)
        if ((code = XKeysymToKeycode(d, keys[i].keysym)))
            for (j = 0; j < sizeof(modifiers)/sizeof(*modifiers); j++)
                XGrabKey(d, code, keys[i].mod | modifiers[j], root,
                        True, GrabModeAsync, GrabModeAsync);

    for (i = 1; i < 4; i += 2)
        for (j = 0; j < sizeof(modifiers)/sizeof(*modifiers); j++)
            XGrabButton(d, i, MOD | modifiers[j], root, True,
                ButtonPressMask|ButtonReleaseMask|PointerMotionMask,
                GrabModeAsync, GrabModeAsync, 0, 0);

    /* Plain click (no modifier) also reaches sowm so we can focus-on-click. */
    for (i = 1; i < 4; i += 2)
        for (j = 0; j < sizeof(modifiers)/sizeof(*modifiers); j++)
            XGrabButton(d, i, modifiers[j], root, True,
                ButtonPressMask,
                GrabModeAsync, GrabModeAsync, 0, 0);

    XFreeModifiermap(modmap);
}

void win_init(void) {
    Window *child;
    unsigned int i, n_child;

    XQueryTree(d, RootWindow(d, DefaultScreen(d)),
               &(Window){0}, &(Window){0}, &child, &n_child);

    for (i = 0;  i < n_child; i++) {
        XSelectInput(d, child[i], StructureNotifyMask|EnterWindowMask);
        XMapWindow(d, child[i]);
        win_add(child[i]);
    }

    XFree(child);
}

int main(void) {
    XEvent ev;

    if (!(d = XOpenDisplay(0))) exit(1);

    signal(SIGCHLD, SIG_IGN);
    XSetErrorHandler(xerror);

    s = DefaultScreen(d);
    root  = RootWindow(d, s);
    sw    = XDisplayWidth(d, s);  //- (2*BORDER_WIDTH);
    sh    = XDisplayHeight(d, s); //- (2*BORDER_WIDTH);

    XSelectInput(d,  root, SubstructureRedirectMask);
    XDefineCursor(d, root, XCreateFontCursor(d, 68));
    input_grab(root);
    win_init();

    while (1 && !XNextEvent(d, &ev)) // 1 && will forever be here.
        if (events[ev.type]) events[ev.type](&ev);
}
