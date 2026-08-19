// The frame that says what is being shared: a click-through red rectangle on the overlay layer,
// steered by lines on a fifo.
//
// It is drawn in a band *around* the region wherever the output has room for one -- the surface is
// inflated by the border width and only that ring is opaque, so the pixels the mirror captures stay
// exactly the ones that were asked for. Drawing on the boundary instead would put a red edge in the
// share.
//
// A side with no room outside is drawn just inside the region instead, which is every side of a
// whole-output share. That red edge is in the share, and is the point: sharing the whole screen is
// the one case where a rectangle only its owner can see is a rectangle nobody can see at all.
//
// The input region is empty, so clicks fall through to whatever is underneath. Without that the
// frame would swallow input to the very window it is outlining.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#define MAX_OUTPUTS 16
#define LINE_MAX_LEN 512

struct output {
	struct wl_output *wl_output;
	struct zxdg_output_v1 *xdg_output;
	char *name;
	// Logical geometry, which is what callers speak and what layer-shell margins are measured in.
	// wl_output only carries a rounded integer scale, so under fractional scaling its geometry is
	// not the compositor's layout; xdg-output reports the layout directly. geometry is kept as the
	// fallback for the rare compositor without xdg-output.
	int32_t x, y;
	int32_t logical_x, logical_y, logical_width, logical_height;
	bool has_logical;
};

static struct {
	struct wl_display *display;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct zxdg_output_manager_v1 *xdg_output_manager;

	struct output outputs[MAX_OUTPUTS];
	int output_count;

	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_buffer *buffer;
	void *pixels;
	size_t pixels_size;

	int border;
	uint32_t color;
	bool fill;
	bool running;
} state = {
	.border = 2,
	.color = 0xffe62626,
	.running = true,
};

static struct output *find_output(const char *name) {
	for (int i = 0; i < state.output_count; i++) {
		if (state.outputs[i].name != NULL && strcmp(state.outputs[i].name, name) == 0) {
			return &state.outputs[i];
		}
	}
	return NULL;
}

static void output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y,
		int32_t physical_width, int32_t physical_height, int32_t subpixel,
		const char *make, const char *model, int32_t transform) {
	(void)output; (void)physical_width; (void)physical_height;
	(void)subpixel; (void)make; (void)model; (void)transform;
	struct output *out = data;
	out->x = x;
	out->y = y;
}

static void output_mode(void *data, struct wl_output *output, uint32_t flags,
		int32_t width, int32_t height, int32_t refresh) {
	(void)data; (void)output; (void)flags; (void)width; (void)height; (void)refresh;
}

static void output_done(void *data, struct wl_output *output) {
	(void)data; (void)output;
}

static void output_scale(void *data, struct wl_output *output, int32_t factor) {
	(void)data; (void)output; (void)factor;
}

static void output_name(void *data, struct wl_output *output, const char *name) {
	(void)output;
	struct output *out = data;
	free(out->name);
	out->name = strdup(name);
}

static void output_description(void *data, struct wl_output *output, const char *description) {
	(void)data; (void)output; (void)description;
}

static void xdg_output_logical_position(void *data, struct zxdg_output_v1 *xdg_output,
		int32_t x, int32_t y) {
	(void)xdg_output;
	struct output *out = data;
	out->logical_x = x;
	out->logical_y = y;
	out->has_logical = true;
}

static void xdg_output_logical_size(void *data, struct zxdg_output_v1 *xdg_output,
		int32_t width, int32_t height) {
	(void)xdg_output;
	struct output *out = data;
	out->logical_width = width;
	out->logical_height = height;
}

static void xdg_output_done(void *data, struct zxdg_output_v1 *xdg_output) {
	(void)data; (void)xdg_output;
}

static void xdg_output_name(void *data, struct zxdg_output_v1 *xdg_output, const char *name) {
	(void)data; (void)xdg_output; (void)name;
}

static void xdg_output_description(void *data, struct zxdg_output_v1 *xdg_output,
		const char *description) {
	(void)data; (void)xdg_output; (void)description;
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
	.logical_position = xdg_output_logical_position,
	.logical_size = xdg_output_logical_size,
	.done = xdg_output_done,
	.name = xdg_output_name,
	.description = xdg_output_description,
};

static const struct wl_output_listener output_listener = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
	.name = output_name,
	.description = output_description,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version) {
	(void)data; (void)version;
	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state.compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		state.shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	} else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
		state.xdg_output_manager = wl_registry_bind(registry, name,
			&zxdg_output_manager_v1_interface, 2);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		state.layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		if (state.output_count >= MAX_OUTPUTS) {
			return;
		}
		struct output *out = &state.outputs[state.output_count++];
		// Version 4 for the name event: it carries the connector name the compositor reports,
		// which is what the caller aims with.
		out->wl_output = wl_registry_bind(registry, name, &wl_output_interface, 4);
		wl_output_add_listener(out->wl_output, &output_listener, out);
	}
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
	(void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void hide(void) {
	if (state.layer_surface != NULL) {
		zwlr_layer_surface_v1_destroy(state.layer_surface);
		state.layer_surface = NULL;
	}
	if (state.surface != NULL) {
		wl_surface_destroy(state.surface);
		state.surface = NULL;
	}
	if (state.buffer != NULL) {
		wl_buffer_destroy(state.buffer);
		state.buffer = NULL;
	}
	if (state.pixels != NULL) {
		munmap(state.pixels, state.pixels_size);
		state.pixels = NULL;
		state.pixels_size = 0;
	}
}

static bool create_buffer(int width, int height) {
	size_t stride = (size_t)width * 4;
	size_t size = stride * (size_t)height;

	int fd = memfd_create("wl-viewfinder", MFD_CLOEXEC);
	if (fd < 0) {
		return false;
	}
	if (ftruncate(fd, (off_t)size) < 0) {
		close(fd);
		return false;
	}
	void *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pixels == MAP_FAILED) {
		close(fd);
		return false;
	}

	uint32_t *row_pixels = pixels;
	for (int row = 0; row < height; row++) {
		for (int col = 0; col < width; col++) {
			bool in_ring = row < state.border || row >= height - state.border ||
				col < state.border || col >= width - state.border;
			row_pixels[(size_t)row * width + col] = (state.fill || in_ring) ? state.color : 0;
		}
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(state.shm, fd, (int32_t)size);
	state.buffer = wl_shm_pool_create_buffer(pool, 0, width, height, (int32_t)stride,
		WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	state.pixels = pixels;
	state.pixels_size = size;
	return true;
}

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *layer_surface,
		uint32_t serial, uint32_t width, uint32_t height) {
	(void)data; (void)width; (void)height;
	zwlr_layer_surface_v1_ack_configure(layer_surface, serial);
	if (state.surface == NULL || state.buffer == NULL) {
		return;
	}
	wl_surface_attach(state.surface, state.buffer, 0, 0);
	wl_surface_damage_buffer(state.surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(state.surface);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *layer_surface) {
	(void)data; (void)layer_surface;
	hide();
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

// x/y/width/height describe the shared region in compositor-global coordinates; the surface placed
// here is the ring around it, hugging the inside of any edge the output leaves no room outside of.
static void show(const char *output_name, int x, int y, int width, int height) {
	struct output *out = find_output(output_name);
	if (out == NULL) {
		fprintf(stderr, "wl-viewfinder-frame: no output named %s\n", output_name);
		return;
	}

	hide();

	// A layer surface is placed relative to its own output, callers report global coordinates.
	int32_t origin_x = out->has_logical ? out->logical_x : out->x;
	int32_t origin_y = out->has_logical ? out->logical_y : out->y;
	// Which sides the output has room to take a band on. An output of unknown size -- no
	// xdg-output, so no logical geometry -- is taken to have room, which is what this did before
	// anybody asked the question.
	bool sized = out->has_logical && out->logical_width > 0 && out->logical_height > 0;
	int left = x - state.border >= origin_x ? state.border : 0;
	int top = y - state.border >= origin_y ? state.border : 0;
	int right = !sized || x + width + state.border <= origin_x + out->logical_width
		? state.border : 0;
	int bottom = !sized || y + height + state.border <= origin_y + out->logical_height
		? state.border : 0;

	// The ring is painted in the outermost border pixels of the surface either way, so a side
	// that was pulled in paints over the first border pixels of the region itself.
	int outer_width = width + left + right;
	int outer_height = height + top + bottom;
	if (!create_buffer(outer_width, outer_height)) {
		fprintf(stderr, "wl-viewfinder-frame: could not allocate %dx%d buffer\n",
			outer_width, outer_height);
		return;
	}

	state.surface = wl_compositor_create_surface(state.compositor);
	struct wl_region *empty = wl_compositor_create_region(state.compositor);
	wl_surface_set_input_region(state.surface, empty);
	wl_region_destroy(empty);

	state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(state.layer_shell, state.surface,
		out->wl_output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wl-viewfinder");
	zwlr_layer_surface_v1_add_listener(state.layer_surface, &layer_surface_listener, NULL);
	zwlr_layer_surface_v1_set_anchor(state.layer_surface,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	zwlr_layer_surface_v1_set_size(state.layer_surface, outer_width, outer_height);
	zwlr_layer_surface_v1_set_margin(state.layer_surface,
		y - origin_y - top, 0, 0, x - origin_x - left);
	// -1 keeps the frame over bars and docks instead of being pushed out of their zone.
	zwlr_layer_surface_v1_set_exclusive_zone(state.layer_surface, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(state.layer_surface,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
	wl_surface_commit(state.surface);
}

// Covers a whole output in opaque black and leaves it there. The blank source a share falls back to
// is an output with nothing on it, and "nothing" is not what a desktop shell draws: it paints its
// wallpaper and its bar onto every output it can see, headless ones included, and that is the
// picture the call would be watching between shares. The overlay layer is above anything a shell
// puts on an output, so this is black whatever else is running.
static void cover(const char *output_name) {
	struct output *out = find_output(output_name);
	if (out == NULL) {
		fprintf(stderr, "wl-viewfinder-frame: no output named %s\n", output_name);
		return;
	}
	if (!out->has_logical || out->logical_width <= 0 || out->logical_height <= 0) {
		fprintf(stderr, "wl-viewfinder-frame: no geometry for %s\n", output_name);
		return;
	}

	hide();
	state.fill = true;
	state.color = 0xff000000;
	if (!create_buffer(out->logical_width, out->logical_height)) {
		fprintf(stderr, "wl-viewfinder-frame: could not allocate %dx%d buffer\n",
			out->logical_width, out->logical_height);
		return;
	}

	state.surface = wl_compositor_create_surface(state.compositor);
	struct wl_region *empty = wl_compositor_create_region(state.compositor);
	wl_surface_set_input_region(state.surface, empty);
	wl_region_destroy(empty);

	state.layer_surface = zwlr_layer_shell_v1_get_layer_surface(state.layer_shell, state.surface,
		out->wl_output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "wl-viewfinder");
	zwlr_layer_surface_v1_add_listener(state.layer_surface, &layer_surface_listener, NULL);
	zwlr_layer_surface_v1_set_anchor(state.layer_surface,
		ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	zwlr_layer_surface_v1_set_size(state.layer_surface, out->logical_width, out->logical_height);
	zwlr_layer_surface_v1_set_exclusive_zone(state.layer_surface, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(state.layer_surface,
		ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
	wl_surface_commit(state.surface);
}

static void handle_line(const char *line) {
	if (strcmp(line, "off") == 0) {
		state.running = false;
		return;
	}
	char output_name[64];
	int x, y, width, height;
	if (sscanf(line, "%63s %d,%d %dx%d", output_name, &x, &y, &width, &height) == 5) {
		show(output_name, x, y, width, height);
	}
}

static void usage(void) {
	fprintf(stderr, "usage: wl-viewfinder-frame [-b border] [-c rrggbb]\n");
	fprintf(stderr, "       wl-viewfinder-frame -l    list outputs as `name x,y wxh` and exit\n");
	fprintf(stderr, "       wl-viewfinder-frame -k output   cover an output in black and stay\n");
	exit(1);
}

// The client is already connected to the compositor to draw, so it is also the one thing here that
// can answer "which outputs are there, and where" without asking a window manager. That is what
// keeps `select` from needing a compositor-specific IPC.
static void list_outputs(void) {
	for (int i = 0; i < state.output_count; i++) {
		struct output *out = &state.outputs[i];
		if (out->name == NULL || !out->has_logical) {
			continue;
		}
		printf("%s %d,%d %dx%d\n", out->name, out->logical_x, out->logical_y,
			out->logical_width, out->logical_height);
	}
}

int main(int argc, char *argv[]) {
	char fifo_path[PATH_MAX];
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	snprintf(fifo_path, sizeof(fifo_path), "%s/wl-viewfinder/frame",
		runtime_dir != NULL ? runtime_dir : "/tmp");

	bool list_only = false;
	const char *cover_output = NULL;
	int opt;
	while ((opt = getopt(argc, argv, "b:c:k:lh")) != -1) {
		switch (opt) {
		case 'l':
			list_only = true;
			break;
		case 'k':
			cover_output = optarg;
			break;
		case 'b':
			state.border = atoi(optarg);
			if (state.border < 1) {
				usage();
			}
			break;
		case 'c':
			state.color = 0xff000000 | (uint32_t)strtoul(optarg, NULL, 16);
			break;
		default:
			usage();
		}
	}

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		fprintf(stderr, "wl-viewfinder-frame: cannot connect to a wayland display\n");
		return 1;
	}

	struct wl_registry *registry = wl_display_get_registry(state.display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(state.display);

	// The manager and the outputs arrive in the same registry burst, so the xdg_output objects can
	// only be made once that burst has been dispatched -- hence the second roundtrip for their
	// events rather than one for everything.
	if (state.xdg_output_manager != NULL) {
		for (int i = 0; i < state.output_count; i++) {
			struct output *out = &state.outputs[i];
			out->xdg_output = zxdg_output_manager_v1_get_xdg_output(state.xdg_output_manager,
				out->wl_output);
			zxdg_output_v1_add_listener(out->xdg_output, &xdg_output_listener, out);
		}
	}
	wl_display_roundtrip(state.display);

	if (list_only) {
		list_outputs();
		wl_display_disconnect(state.display);
		return 0;
	}

	if (state.compositor == NULL || state.shm == NULL || state.layer_shell == NULL) {
		fprintf(stderr, "wl-viewfinder-frame: compositor does not support wlr-layer-shell\n");
		return 1;
	}

	if (cover_output != NULL) {
		cover(cover_output);
		while (state.running && wl_display_dispatch(state.display) >= 0) {
		}
		hide();
		wl_display_disconnect(state.display);
		return 0;
	}

	// Opened read-write so the frame is always its own writer: every caller is a separate,
	// short-lived writer, and without this the reader would see EOF each time one closed.
	int fifo_fd = open(fifo_path, O_RDWR | O_CLOEXEC);
	if (fifo_fd < 0) {
		fprintf(stderr, "wl-viewfinder-frame: cannot open %s: %s\n", fifo_path, strerror(errno));
		return 1;
	}

	char pending[LINE_MAX_LEN];
	size_t pending_len = 0;

	while (state.running) {
		wl_display_flush(state.display);

		struct pollfd fds[2] = {
			{ .fd = wl_display_get_fd(state.display), .events = POLLIN },
			{ .fd = fifo_fd, .events = POLLIN },
		};
		if (poll(fds, 2, -1) < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}

		if ((fds[0].revents & POLLIN) && wl_display_dispatch(state.display) < 0) {
			break;
		}

		if (fds[1].revents & POLLIN) {
			char chunk[LINE_MAX_LEN];
			ssize_t got = read(fifo_fd, chunk, sizeof(chunk));
			if (got <= 0) {
				continue;
			}
			for (ssize_t i = 0; i < got && state.running; i++) {
				if (chunk[i] == '\n') {
					pending[pending_len] = '\0';
					handle_line(pending);
					pending_len = 0;
				} else if (pending_len < sizeof(pending) - 1) {
					pending[pending_len++] = chunk[i];
				}
			}
		}
	}

	hide();
	wl_display_disconnect(state.display);
	return 0;
}
