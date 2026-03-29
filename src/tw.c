/*
 * Copyright (c) 2008 Kanru Chen
 *  
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <gtk/gtk.h>
#include <glib.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <glib/gstdio.h>

struct unit
{
    gchar *v;
    double x;
    double y;
};
typedef struct unit unit_t;

/* Global settings */
static double speed = 20.; /* pixel */
static int hp = 100;
static int freq = 3000;
static gchar *input_file;

/* Internal stuff */
static double fps = 30.;
static int gwidth, gheight;
static GArray *units;
static GPtrArray *input_units;
static int score;
static double move_offset;

static GOptionEntry entries[] = 
{
    { "file", 'i', 0, G_OPTION_ARG_FILENAME, &input_file, "Input file for user defined word list", "FILE" },
    { "speed", 's', 0, G_OPTION_ARG_DOUBLE, &speed, "Initial speed, default 20.0", "SPEED"},
    { "hp", 0, 0, G_OPTION_ARG_INT, &hp, "Initial hit point, default 100", "HP"},
    { "freq", 'f', 0, G_OPTION_ARG_INT, &freq, "Words generation frequency in microsecond, default 3000", "FREQ"},
    { NULL }
};

/**
 * Range: 4421-7D4B
 * EUC-TW: 0x8EA18080
 */
static gchar* generate_word_from_big5()
{
    guchar in[] = {0x8E, 0xA1, 0x80, 0x80};
    gchar *out;
    guchar w;
    do {
        w = random() % 0x7E;
    } while (w <= 0x44 || w > 0x7D);
    in[2] += w;
    do {
        w = random() % 0x7E;
    } while (w <= 0x20 || w > (in[2] == 0xFD ? 0x4B : 0x7D));
    in[3] += w;
    out = g_convert(in, sizeof(in), "UTF-8", "EUC-TW", NULL, NULL, NULL);
    if (out)
        return out;
    else
        return generate_word_from_big5();
}

static gchar* generate_word_from_file()
{
    int idx;
    idx = random() % input_units->len;
    return g_strdup(g_ptr_array_index(input_units, idx));
}

static gchar* generate_word()
{
    if (input_units)
        return generate_word_from_file();
    else
        return generate_word_from_big5();
}

static void display_gameover(cairo_t *cr)
{
    char s[10];
    PangoLayout *layout;
    PangoFontDescription *desc;
    int w, h;
    layout = pango_cairo_create_layout(cr);
    desc = pango_font_description_from_string("Serif Bold 72");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    cairo_save(cr);
    cairo_set_source_rgb(cr, 1., 0., 0.);
    pango_layout_set_text(layout, "GAME OVER", -1);
    pango_layout_get_pixel_size(layout, &w, &h);
    cairo_move_to(cr, -w/2., -h/2.);
    pango_cairo_show_layout (cr, layout);
    cairo_restore(cr);
    g_object_unref (layout);
}

static void display_score(cairo_t *cr)
{
    char s[10];
    PangoLayout *layout;
    PangoFontDescription *desc;
    int w, h;
    layout = pango_cairo_create_layout(cr);
    desc = pango_font_description_from_string("Sans Bold 35");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    cairo_save(cr);
    cairo_set_source_rgba(cr, 1., 1., 1., .5);
    g_snprintf(s, sizeof(s), "%03d", score);
    pango_layout_set_text(layout, s, -1);
    pango_layout_get_pixel_size(layout, &w, &h);
    cairo_move_to(cr, -w/2., -gheight*5./12.-h/2.);
    pango_cairo_show_layout (cr, layout);
    g_snprintf(s, sizeof(s), "%03d", hp);
    pango_layout_set_text(layout, s, -1);
    pango_layout_get_pixel_size(layout, &w, &h);
    cairo_move_to(cr, -w/2., gheight*5./12.-h/2.);
    pango_cairo_show_layout (cr, layout);
    cairo_restore(cr);
    g_object_unref (layout);
}

static void display_units(cairo_t *cr)
{
    int i, h, w;
    unit_t u;
    PangoLayout *layout;
    PangoFontDescription *desc;
    layout = pango_cairo_create_layout(cr);
    desc = pango_font_description_from_string("Sans 20");
    pango_layout_set_font_description(layout, desc);
    pango_font_description_free(desc);
    cairo_save(cr);
    cairo_set_source_rgb(cr, 1., 1., 1.);
    if(units)
        for(i = 0; i < units->len; ++i) {
            u = g_array_index(units, unit_t, i);
            pango_layout_set_text(layout, u.v, -1);
            pango_layout_get_pixel_size(layout, &w, &h);
            cairo_move_to(cr, u.x-w/2., u.y-h/2.);
            pango_cairo_show_layout (cr, layout);
        }
    cairo_restore(cr);
    g_object_unref (layout);
}

static void draw_func(GtkDrawingArea *area, cairo_t *cr, int width, int height, gpointer user_data)
{
    gwidth = width;
    gheight = height;
    cairo_set_source_rgb(cr, 0., 0., 0.);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.5, 0., 0.);
    cairo_translate(cr, gwidth/2., gheight/2.);
    cairo_arc(cr, 0., 0., 100., 0., 2.*M_PI);
    cairo_stroke(cr);
    cairo_arc(cr, 0., 0., 50., 0., 2.*M_PI);
    cairo_fill(cr);
    display_score(cr);
    display_units(cr);
    if(hp <= 0)
        display_gameover(cr);
}

static void im_commit(GtkIMContext *imctx, gchar *str, gpointer ud)
{
    int i;
    unit_t u;
    if(units)
        for(i = 0; i < units->len; ++i) {
            u = g_array_index(units, unit_t, i);
            if (!strcmp(str, u.v)) {
                g_free(u.v);
                g_array_remove_index_fast(units, i);
                score++;
                break;
            }
        }
    gtk_widget_queue_draw(GTK_WIDGET(ud));
}

static gboolean on_timeout(gpointer ud)
{
    int i;
    float d;
    unit_t *u;
    GArray *s;
    if(!units)
        units = g_array_new(TRUE, TRUE, sizeof(unit_t));
    s = g_array_new(TRUE, TRUE, sizeof(int));
    for(i = 0; i < units->len; ++i) {
        u = &g_array_index(units, unit_t, i);
        d = sqrt(u->x*u->x + u->y*u->y);
        u->x -= u->x/d*move_offset;
        u->y -= u->y/d*move_offset;
        if(d < 50)
            g_array_prepend_val(s, i);
    }
    for(i = 0; i < s->len; ++i) {
        g_free(g_array_index(units, unit_t, g_array_index(s, int, i)).v);
        g_array_remove_index_fast(units, g_array_index(s, int, i));
        hp--;
    }
    g_array_free(s, TRUE);
    gtk_widget_queue_draw(GTK_WIDGET(ud));
    if(hp <=0)
        return FALSE;
    else
        return TRUE;
}

static gboolean on_timeout2(gpointer ud)
{
    double a;
    int i;
    unit_t t;
    for(i = 0; i < 3; ++i) {
        a = (random() % 360) / 360. * 2*M_PI;
        t.v = generate_word();
        t.x = cos(a) * gwidth/2.;
        t.y = sin(a) * gwidth/2.;
        g_array_append_val(units, t);
    }
    if(hp <=0)
        return FALSE;
    else
        return TRUE;
}

static void parse_input_file(gchar *file)
{
    gchar inc[11];
    gchar *it = inc;
    gchar *end = &inc[10];
    int fin = -1;
    ssize_t rn = 0;

    fin = g_open(file, O_RDONLY);
    if (fin<0) {
        perror(file);
        return;
    }
    if (!input_units)
        input_units = g_ptr_array_new();
    memset(inc, 0, sizeof(inc));
    for (;;) {
        rn = read(fin, it, sizeof(gchar));
        if (!rn) {
            close(fin);
            break;
        }
        if (it != end && !isspace(*it))
                it += rn;
        else {
            memset(inc, 0, sizeof(inc));
            it = inc;
            continue;
        }
        if (g_utf8_validate(inc, -1, NULL)) {
            g_ptr_array_add(input_units, g_strdup(inc));
            memset(inc, 0, sizeof(inc));
            it = inc;
        }
    }
}

static void activate(GtkApplication *app, gpointer user_data)
{
    GtkWidget *frame, *da;
    GtkIMContext *imctx;
    GtkEventController *key_controller;

    frame = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(frame), "Type War");

    da = gtk_drawing_area_new();
    gtk_widget_set_focusable(da, TRUE);
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(da), draw_func, NULL, NULL);
    gtk_window_set_child(GTK_WINDOW(frame), da);

    imctx = gtk_im_multicontext_new();
    gtk_im_context_set_use_preedit(imctx, FALSE);
    g_signal_connect(G_OBJECT(imctx), "commit", G_CALLBACK(im_commit), da);

    key_controller = gtk_event_controller_key_new();
    gtk_event_controller_key_set_im_context(GTK_EVENT_CONTROLLER_KEY(key_controller), imctx);
    gtk_im_context_set_client_widget(imctx, da);
    gtk_widget_add_controller(da, key_controller);

    gtk_window_present(GTK_WINDOW(frame));
    gtk_widget_grab_focus(da);

    g_timeout_add(33, on_timeout, da);
    g_timeout_add(freq, on_timeout2, NULL);
}

int main(int argc, char *argv[])
{
    GtkApplication *app;
    int status;

    GError *error = NULL;
    GOptionContext *context;

    context = g_option_context_new("- chinese typing practice");
    g_option_context_add_main_entries(context, entries, NULL);
    g_option_context_set_ignore_unknown_options(context, TRUE);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message);
        g_error_free(error);
        g_option_context_free(context);
        return 1;
    }
    g_option_context_free(context);

    if (input_file)
        parse_input_file(input_file);

    srandom(time(NULL));
    move_offset = speed / fps;

    app = gtk_application_new("org.kanru.typewar", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);

    return status;
}
