/*
 * gmain.c: gtk main routines for pho, an image viewer.
 *
 * Copyright 2002 by Akkana Peck.
 * You are free to use or modify this code under the Gnu Public License.
 */

#include "pho.h"
#include "exif/phoexif.h"

#include <stdlib.h>       // for getenv()
#include <stdio.h>
#include <string.h>

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

extern void UpdateInfoDialog();

static GtkWidget *win = 0;
static GtkWidget *drawingArea = 0;
    
static int gFullScreenMode = 0;

/* ShowImage assumes gImage already contains the right GdkPixbuf.
 */
void ShowImage()
{
    static int oldXsize = -1;
    static int oldYsize = -1;
    char title[BUFSIZ];
#define TITLELEN ((sizeof title) / (sizeof *title))

    if (drawingArea == 0 || drawingArea->window == 0 || gImage == 0)
        return;
    if (oldXsize < 0) oldXsize = XSize;
    if (oldYsize < 0) oldYsize = YSize;

    // If we're coming back to normal from fullscreen mode,
    // we need to restore sizes.
    if (!gFullScreenMode && (XSize > realXSize || YSize > realYSize))
    {
        GdkPixbuf* newimage;
        XSize = realXSize;
        YSize = realYSize;
        newimage = gdk_pixbuf_scale_simple(gImage, XSize, YSize,
                                           GDK_INTERP_NEAREST);
        if (gImage)
            gdk_pixbuf_unref(gImage);
        gImage = newimage;
        resized = 0;
    }

    // Scale it down if needed.
    else if (XSize > MonitorWidth || YSize > MonitorHeight
             || (gFullScreenMode
                 && XSize < MonitorWidth && YSize < MonitorWidth))
    {
        GdkPixbuf* newimage;
        double xratio = (double)XSize / MonitorWidth;
        double yratio = (double)YSize / MonitorHeight;
        double ratio = (xratio > yratio ? xratio : yratio);

        XSize = (double)XSize / ratio;
        YSize = (double)YSize / ratio;

        newimage = gdk_pixbuf_scale_simple(gImage, XSize, YSize,
                                           GDK_INTERP_NEAREST);
        if (gImage)
            gdk_pixbuf_unref(gImage);
        gImage = newimage;
        resized = 1;
    }

    if (XSize != oldXsize || YSize != oldYsize)
    {
        gint x, y;
        gint nx = -1, ny = -1;

        gdk_window_resize(win->window, XSize, YSize);

        // If we  resized, see if we need to move:
        gdk_window_get_position(win->window, &x, &y);
        if (x + XSize >= MonitorWidth)
            nx = MonitorWidth - XSize;
        if (y + YSize >= MonitorHeight)
            ny = MonitorHeight - YSize;
        if (x >= 0 || y >= 0) {
            //printf("Moving from (%d, %d) to (%d, %d)\n", x, y, nx, ny);
            gdk_window_move(win->window, (nx >= 0 ? nx : x), (ny >= 0 ? ny : y));
            //gtk_widget_set_uposition(GTK_WIDGET(win), (nx >= 0 ? nx : x), (ny >= 0 ? ny : y));
        }
    }

    gdk_pixbuf_render_to_drawable(gImage, drawingArea->window,
                     drawingArea->style->fg_gc[GTK_WIDGET_STATE(drawingArea)],
                                  0, 0, 0, 0, XSize, YSize,
                                  GDK_RGB_DITHER_NONE, 0, 0);

    // Update the titlebar
    sprintf(title, "pho: %s (%d x %d)", ArgV[ArgP], realXSize, realYSize);
    if (HasExif())
    {
        const char* date = ExifGetString(ExifDate);
        if (date && date[0]) {
            /* Make sure there's room */
            if (strlen(title) + strlen(date) + 3 < TITLELEN)
            strcat(title, " (");
            strcat(title, date);
            strcat(title, ")");
        }
    }
    if (gFullScreenMode && (strlen(title) + 13 < TITLELEN))
        strcat(title, " (fullscreen)");

    gtk_window_set_title(GTK_WINDOW(win), title);

    UpdateInfoDialog();
}

static void ToggleFullScreenMode()
{
    gFullScreenMode = !gFullScreenMode;
    ShowImage();
}

static gint HandleDelete(GtkWidget* widget, GdkEventKey* event, gpointer data)
{
    /* If you return FALSE in the "delete_event" signal handler,
     * GTK will emit the "destroy" signal. Returning TRUE means
     * you don't want the window to be destroyed.
     * This is useful for popping up 'are you sure you want to quit?'
     * type dialogs. */

    /* Change TRUE to FALSE and the main window will be destroyed with
     * a "delete_event". */
    return FALSE;
}

static gint HandleExpose(GtkWidget* widget, GdkEventExpose* event)
{
    ShowImage();
    return TRUE;
}

void EndSession()
{
    PrintNotes();
    gtk_main_quit();
}

static gint HandleDestroy(GtkWidget* widget, GdkEventKey* event, gpointer data)
{
    EndSession();
    return TRUE;
}

static int CallExternal(char* progname, char* filename)
{
    /* Seems like there ought to be a better way, calling fork/execl directly;
     * but then there's no good way to get the notification if the first
     * command (gimp-remote) fails to execute ...
     */
    char buf[BUFSIZ];
    snprintf(buf, sizeof buf, "%s %s &", progname, filename);
    return system(buf);
}

static gint HandleKeyPress(GtkWidget* widget, GdkEventKey* event)
{
    int i;
    switch (event->keyval)
    {
      case GDK_d:
          DeleteImage();
          break;
      case GDK_space:
          if (NextImage() != 0) {
              if (PromptDialog("Quit pho?", "Quit", "Continue",
                               "qx \n", "c ") != 0)
                  EndSession();
          }
          else
              ShowImage();
          return TRUE;
      case GDK_BackSpace:
      case GDK_minus:
          if (PrevImage() == 0)
              ShowImage();
          return TRUE;
      case GDK_Home:
          ArgP = 0;
          if (NextImage() == 0)
              ShowImage();
          return TRUE;
      case GDK_f:
          ToggleFullScreenMode();
          break;
      case GDK_0:
      case GDK_1:
      case GDK_2:
      case GDK_3:
      case GDK_4:
      case GDK_5:
      case GDK_6:
      case GDK_7:
      case GDK_8:
      case GDK_9:
          SetNoteFlag(ArgP, event->keyval - GDK_0);
          return TRUE;
      case GDK_t:   // make life easier for xv users
      case GDK_r:
      case GDK_Right:
      case GDK_KP_Right:
          RotateImage(90);
          return TRUE;
      case GDK_T:   // make life easier for xv users
      case GDK_R:
      case GDK_l:
      case GDK_L:
      case GDK_Left:
      case GDK_KP_Left:
          RotateImage(-90);
          return TRUE;
      case GDK_Up:
          RotateImage(180);
          break;
      case GDK_g:  // start gimp
          if ((i = CallExternal("gimp-remotte -n", ArgV[ArgP])) != 0) {
              i = CallExternal("gimp", ArgV[ArgP]);
              printf("Called gimp, returned %d\n", i);
          }
          else printf("Called gimp-remote, returned %d\n", i);
          break;
      case GDK_Escape:
      case GDK_q:
          EndSession();
          return TRUE;
      case GDK_i:
          ToggleInfo();
          return TRUE;
      default:
          if (Debug)
              printf("Don't know key 0x%lu\n", (unsigned long)(event->keyval));
          return FALSE;
    }
    // Keep gcc 2.95 happy:
    return FALSE;
}

int main(int argc, char** argv)
{
    while (argc > 1 && argv[1][0] == '-')
    {
        if (argv[1][1] == 'd')
            Debug = 1;
        else if (argv[1][1] == 'v' || argv[1][1] == 'h')
            Usage();
        else Usage();
        --argc;
        ++argv;
    }

    if (argc <= 1)
        Usage();

    ArgV = argv;
    ArgC = argc;
    ArgP = 0;

    // Allocate space to store notes about each image.
    MakeNotesList(ArgC);

    // Load the first image (make sure we have at least one):
    if (NextImage() != 0)
        exit(1);

    // See http://www.gtk.org/tutorial
    gtk_init(&ArgC, &ArgV);
    
    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    
    /* Window manager delete */
    gtk_signal_connect(GTK_OBJECT(win), "delete_event",
                       (GtkSignalFunc)HandleDelete, 0);

    /* This event occurs when we call gtk_widget_destroy() on the window,
     * or if we return FALSE in the "delete_event" callback.
     */
    gtk_signal_connect(GTK_OBJECT(win), "destroy",
                       (GtkSignalFunc)HandleDestroy, 0);

    /* KeyPress events on the drawing area don't come through --
     * they have to be on the window.
     */
    gtk_signal_connect(GTK_OBJECT(win), "key_press_event",
                       (GtkSignalFunc)HandleKeyPress, 0);

    drawingArea = gtk_drawing_area_new();

    gtk_container_add(GTK_CONTAINER(win), drawingArea);
    gtk_widget_show(drawingArea);

#if 0
    // This doesn't seem to make any difference
    gtk_widget_set_events(drawingArea,
                          GDK_EXPOSURE_MASK
                          | GDK_KEY_PRESS_MASK
                          | GDK_STRUCTURE_MASK );
#endif

    gtk_signal_connect(GTK_OBJECT(drawingArea), "expose_event",
                       (GtkSignalFunc)HandleExpose, 0);

    MonitorWidth = gdk_screen_width();
    MonitorHeight = gdk_screen_height();

    // Must init rgb system explicitly, else we'll crash
    // in the first gdk_pixbuf_render_to_drawable(),
    // calling gdk_draw_rgb_image_dithalign():
    gdk_rgb_init();

    gtk_window_set_default_size(GTK_WINDOW(win), XSize, YSize);

    // Now we know we have something to show, so do it:
    gtk_widget_show(win);

    gtk_main();
    return 0;
}

