/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: requesters - a question with buttons, a question asking for a name -
 * in the desktop's own framed windows.
 *
 * The buttons are given as Ambient gives them: "*_Replace|Replace All|_Skip|_Cancel",
 * "|" between them, "_" before a button's key, "*" before the default one (Return); the
 * last is the one Escape and the close gadget choose. The answer is the button's index.
 */
#include "desktop.h"

#include <string.h>

typedef struct Req {
    Nv_Window *w;
    Evas_Object *entry;
    int count, def;
    Nv_Request_Cb cb;
    Nv_Request_String_Cb scb;
    void *data;
    bool answered;
} Req;

static void answer(Req *r, int choice)
{
    if (r->answered)
        return;
    r->answered = true;
    if (r->scb) {
        char *text = NULL;
        if (choice != r->count - 1 && r->entry)
            text = elm_entry_markup_to_utf8(elm_entry_entry_get(r->entry));
        r->scb(r->data, choice == r->count - 1 ? -1 : choice, text);
        free(text);
    } else if (r->cb) {
        r->cb(r->data, choice);
    }
    Nv_Window *w = r->w;
    r->w = NULL;
    nv_window_close(w);
}

static void on_button(void *data, Evas_Object *o, void *ev)
{
    (void)ev;
    Req *r = data;
    answer(r, (int)(intptr_t)evas_object_data_get(o, "choice"));
}

static void on_close(void *data, Nv_Window *w)
{
    Req *r = data;
    if (!r->answered) {
        r->answered = true;
        if (r->scb)
            r->scb(r->data, -1, NULL);
        else if (r->cb)
            r->cb(r->data, r->count - 1);
    }
    evas_object_del(w->win);
    free(r);
}

static void on_key(void *data, Evas *e, Evas_Object *o, void *event)
{
    (void)e;
    (void)o;
    Req *r = data;
    Evas_Event_Key_Down *ev = event;
    if (!strcmp(ev->key, "Escape"))
        answer(r, r->count - 1);
    else if (!strcmp(ev->key, "Return") || !strcmp(ev->key, "KP_Enter"))
        answer(r, r->def);
}

static void on_activated(void *data, Evas_Object *o, void *ev)
{
    (void)o;
    (void)ev;
    Req *r = data;
    answer(r, r->def);
}

/* the buttons' row from "*_Replace|_Skip|_Cancel" */
static Evas_Object *buttons_row(Req *r, const char *buttons)
{
    Evas_Object *row = elm_box_add(r->w->win);
    elm_box_horizontal_set(row, EINA_TRUE);
    elm_box_homogeneous_set(row, EINA_TRUE);
    elm_box_padding_set(row, 8, 0);
    evas_object_size_hint_weight_set(row, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(row, EVAS_HINT_FILL, 1.0);
    char *copy = strdup(buttons), *save = NULL;
    int i = 0;
    Evas_Object *defb = NULL;
    for (char *b = strtok_r(copy, "|", &save); b; b = strtok_r(NULL, "|", &save), i++) {
        bool is_def = *b == '*';
        if (is_def) {
            b++;
            r->def = i;
        }
        char label[128];
        size_t n = 0;
        for (const char *p = b; *p && n < sizeof label - 1; p++)
            if (*p != '_')
                label[n++] = *p;
        label[n] = 0;
        Evas_Object *bt = elm_button_add(r->w->win);
        elm_object_text_set(bt, label);
        evas_object_data_set(bt, "choice", (void *)(intptr_t)i);
        evas_object_size_hint_weight_set(bt, EVAS_HINT_EXPAND, 0);
        evas_object_size_hint_align_set(bt, EVAS_HINT_FILL, 0.5);
        evas_object_smart_callback_add(bt, "clicked", on_button, r);
        elm_box_pack_end(row, bt);
        evas_object_show(bt);
        if (is_def || !defb)
            defb = is_def ? bt : defb ? defb : bt;
    }
    free(copy);
    r->count = i;
    if (defb && !r->entry)
        elm_object_focus_set(defb, EINA_TRUE);
    return row;
}

static Req *req_new(const char *title, const char *text, int w, int h)
{
    Req *r = calloc(1, sizeof *r);
    r->w = nv_window_new("nirvana-requester", title, w, h);
    r->w->on_close = on_close;
    r->w->data = r;
    (void)text;
    return r;
}

static Evas_Object *text_label(Req *r, const char *text, bool bold)
{
    Evas_Object *lab = elm_label_add(r->w->win);
    elm_label_line_wrap_set(lab, ELM_WRAP_WORD);
    char *markup = elm_entry_utf8_to_markup(text);
    char buf[8192];
    snprintf(buf, sizeof buf, bold ? "<align=center><b>%s</b></align>" : "<align=center>%s</align>", markup);
    free(markup);
    elm_object_text_set(lab, buf);
    evas_object_size_hint_weight_set(lab, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    evas_object_size_hint_align_set(lab, EVAS_HINT_FILL, 0.5);
    evas_object_show(lab);
    return lab;
}

static void finish(Req *r, Evas_Object *box)
{
    nv_window_content_set(r->w, box);
    evas_object_show(box);
    evas_object_event_callback_add(r->w->win, EVAS_CALLBACK_KEY_DOWN, on_key, r);
    nv_window_show(r->w);
    /* (a question is answered first: above the others, with the keyboard) */
    elm_win_raise(r->w->win);
    elm_win_activate(r->w->win);
}

void nv_request(const char *title, const char *text, const char *buttons, Nv_Request_Cb cb, void *data)
{
    Req *r = req_new(title, text, 420, 150);
    r->cb = cb;
    r->data = data;
    Evas_Object *box = elm_box_add(r->w->win);
    elm_box_padding_set(box, 0, 12);
    evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    elm_box_pack_end(box, text_label(r, text, false));
    Evas_Object *row = buttons_row(r, buttons);
    elm_box_pack_end(box, row);
    evas_object_show(row);
    finish(r, box);
}

void nv_request_string(const char *title, const char *label, const char *initial, const char *buttons,
                       bool select_stem, Nv_Request_String_Cb cb, void *data)
{
    Req *r = req_new(title, label, 380, 140);
    r->scb = cb;
    r->data = data;
    Evas_Object *box = elm_box_add(r->w->win);
    elm_box_padding_set(box, 0, 8);
    evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
    elm_box_pack_end(box, text_label(r, label, true));
    r->entry = elm_entry_add(r->w->win);
    elm_entry_single_line_set(r->entry, EINA_TRUE);
    elm_entry_scrollable_set(r->entry, EINA_TRUE);
    char *markup = elm_entry_utf8_to_markup(initial ? initial : "");
    elm_entry_entry_set(r->entry, markup);
    free(markup);
    evas_object_size_hint_weight_set(r->entry, EVAS_HINT_EXPAND, 0);
    evas_object_size_hint_align_set(r->entry, EVAS_HINT_FILL, 0.5);
    evas_object_smart_callback_add(r->entry, "activated", on_activated, r);
    elm_box_pack_end(box, r->entry);
    evas_object_show(r->entry);
    Evas_Object *row = buttons_row(r, buttons);
    elm_box_pack_end(box, row);
    evas_object_show(row);
    finish(r, box);
    /* the name ready to be typed over: all of it, or its stem (not ".txt") */
    elm_object_focus_set(r->entry, EINA_TRUE);
    const char *dot = initial ? strrchr(initial, '.') : NULL;
    if (select_stem && dot && dot != initial)
        elm_entry_select_region_set(r->entry, 0, (int)eina_unicode_utf8_get_len(initial) -
                                                     (int)eina_unicode_utf8_get_len(dot));
    else
        elm_entry_select_all(r->entry);
}
