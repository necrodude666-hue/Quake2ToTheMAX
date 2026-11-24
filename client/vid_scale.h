/* BDGN 3.50: 2D scaling (menus, HUD, console, crosshair). The client lays 2D out on a virtual screen of
   real size / scale; ref_gl draws it scaled up by that whole number (cvar scr_scale_active).
   Pure functions, owned by win32/vid_dll.c; tested in test/vid_scale_test.c. */
#ifndef VID_SCALE_H
#define VID_SCALE_H

#define VID_SCALE_REFERENCE_HEIGHT	600		/* auto: 2D about as big on screen as at 800x600 */
#define VID_SCALE_MAX				8

#define VID_SCALE_MIN_WIDTH			640		/* the virtual screen never gets smaller: the options menu */
#define VID_SCALE_MIN_HEIGHT		480		/* needs about 380 rows */

/* setting is scr_scale: 0 (or less) = auto (height / 600, rounded), else the wanted scale.
   Returns the scale to use, >= 1, lowered until the virtual screen is at least 640x480. */
static int VID_Scale2D (int width, int height, int setting)
{
	int scale = setting > 0 ? setting : (height + VID_SCALE_REFERENCE_HEIGHT / 2) / VID_SCALE_REFERENCE_HEIGHT;
	if (scale > VID_SCALE_MAX)
		scale = VID_SCALE_MAX;
	while (scale > 1 && (width / scale < VID_SCALE_MIN_WIDTH || height / scale < VID_SCALE_MIN_HEIGHT))
		scale--;
	return scale < 1 ? 1 : scale;
}

/* A rectangle in virtual 2D coordinates to real pixels. An edge on the virtual screen's right or bottom
   border goes to the real border, as the real size need not be a multiple of the scale. */
static void VID_RectToReal (int *x, int *y, int *width, int *height, int scale,
	int virtual_width, int virtual_height, int real_width, int real_height)
{
	int x2 = *x + *width, y2 = *y + *height;
	x2 = x2 >= virtual_width ? real_width : x2 * scale;
	y2 = y2 >= virtual_height ? real_height : y2 * scale;
	*x *= scale;
	*y *= scale;
	*width = x2 - *x;
	*height = y2 - *y;
}

/* The UI is laid out in output pixels even when Software draws a smaller
   framebuffer. Project both edges so neighbouring rectangles still meet. */
static void VID_RectToRender (int *x, int *y, int *width, int *height, int scale,
	int virtual_width, int virtual_height, int output_width, int output_height,
	int render_width, int render_height)
{
	int right, bottom;
	VID_RectToReal(x, y, width, height, scale, virtual_width, virtual_height, output_width, output_height);
	if (render_width != output_width)
	{
		right = (*x + *width) * render_width / output_width;
		*x = *x * render_width / output_width;
		*width = right - *x;
	}
	if (render_height != output_height)
	{
		bottom = (*y + *height) * render_height / output_height;
		*y = *y * render_height / output_height;
		*height = bottom - *y;
	}
}

#endif
