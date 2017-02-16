#include <stdio.h>

#include <obs-module.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <graphics/image-file.h>
#include <util/dstr.h>
#include <pthread.h>
#include <util/pipe.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("image-compare", "en-US")

#define SETTING_IMAGE_PATHS            "images"
#define SETTING_COMMAND                "command"
#define SETTING_DEBUG_ENABLED          "debug_enabled"
#define SETTING_DEBUG_IMAGE            "debug_image"

#define TEXT_IMAGE_PATH                obs_module_text("Images")
#define TEXT_COMMAND                   obs_module_text("Command")
#define TEXT_DEBUG_ENABLED             obs_module_text("DebugEnabled")
#define TEXT_DEBUG_IMAGE               obs_module_text("DebugImage")

#define TEXT_PATH_IMAGES               obs_module_text("BrowsePath.Images")
#define TEXT_PATH_ALL_FILES            obs_module_text("BrowsePath.AllFiles")

#define MAX_IMAGES                     31

char *PARAMETER_NAMES[] = {
	"image0",
	"image1",
	"image2",
	"image3",
	"image4",
	"image5",
	"image6",
};

struct image_compare_data {
	/* Config */
	size_t                          count;
	char                           *paths[MAX_IMAGES];
	char                           *command;
	int                             debug;

	/* Runtime */
	obs_source_t                   *context;
	gs_effect_t                    *effect;

	bool                            loaded;

	uint32_t                        cx;
	uint32_t                        cy;

	gs_texrender_t                 *texrender;
	gs_stagesurf_t                 *stagesurf;

	gs_image_file_t                 image[MAX_IMAGES];

	uint32_t                        lastMatch;
};

static const char *image_compare_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("ImageCompare");
}

static void image_compare_load(struct image_compare_data *filter) {
	if (filter->loaded)
		return;

	/* Allocate */
	for (size_t i = 0; i < filter->count; i++) {
		blog(LOG_INFO, "Loading image %s", filter->paths[i]);
		gs_image_file_init(&filter->image[i], filter->paths[i]);
		if (i == 0) {
			filter->cx = filter->image[i].cx;
			filter->cy = filter->image[i].cy;
		}
	}

	obs_enter_graphics();
	for (size_t i = 0; i < filter->count; i++) {
		gs_image_file_init_texture(&filter->image[i]);
	}
	filter->stagesurf = gs_stagesurface_create(filter->cx, filter->cy, GS_R8);
	obs_leave_graphics();

	filter->loaded = true;
}

static void image_compare_unload(struct image_compare_data *filter) {
	if (!filter->loaded)
		return;

	obs_enter_graphics();
	gs_stagesurface_destroy(filter->stagesurf);
	for (size_t i = 0; i < filter->count; i++) {
		gs_image_file_free(&filter->image[i]);
	}
	obs_leave_graphics();

	filter->loaded = false;
}

static void image_compare_free(struct image_compare_data *filter) {
	image_compare_unload(filter);

	for (size_t i = 0; i < filter->count; i++) {
		bfree(filter->paths[i]);
		filter->paths[i] = NULL;
	}

	bfree(filter->command);
	filter->command = NULL;
}

static void image_compare_update(void *data, obs_data_t *settings)
{
	struct image_compare_data *filter = data;
	obs_data_array_t *array;

	image_compare_free(filter);

	/* Get Config */
	array = obs_data_get_array(settings, SETTING_IMAGE_PATHS);
	filter->count = obs_data_array_count(array);
	if (filter->count > MAX_IMAGES)
		filter->count = MAX_IMAGES;
	for (size_t i = 0; i < filter->count; i++) {
		obs_data_t *item = obs_data_array_item(array, i);
		const char *path = obs_data_get_string(item, "value");

		filter->paths[i] = bstrdup(path);

		obs_data_release(item);
	}
	obs_data_array_release(array);
	filter->command = bstrdup(obs_data_get_string(settings, SETTING_COMMAND));
	if (obs_data_get_bool(settings, SETTING_DEBUG_ENABLED)) {
		filter->debug = obs_data_get_int(settings, SETTING_DEBUG_IMAGE) - 1;
	} else {
		filter->debug = -1;
	}

	if (obs_source_enabled(filter->context))
		image_compare_load(filter);
}

static void image_compare_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, SETTING_DEBUG_IMAGE, 1);
}

static bool image_compare_debug_enabled_changed(obs_properties_t *props,
		obs_property_t *property, obs_data_t *settings)
{
	obs_property_t *prop;

	bool enabled = obs_data_get_bool(settings, SETTING_DEBUG_ENABLED);

	prop = obs_properties_get(props, SETTING_DEBUG_IMAGE);
	obs_property_set_visible(prop, enabled);

	UNUSED_PARAMETER(property);
	return true;
}

#define IMAGE_FILTER_EXTENSIONS \
	" (*.bmp *.jpg *.jpeg *.tga *.gif *.png)"

static obs_properties_t *image_compare_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *prop;
	struct dstr filter_str = {0};

	dstr_copy(&filter_str, TEXT_PATH_IMAGES);
	dstr_cat(&filter_str, IMAGE_FILTER_EXTENSIONS ";;");
	dstr_cat(&filter_str, TEXT_PATH_ALL_FILES);
	dstr_cat(&filter_str, " (*.*)");
	obs_properties_add_editable_list(props, SETTING_IMAGE_PATHS, TEXT_IMAGE_PATH,
			OBS_EDITABLE_LIST_TYPE_FILES, filter_str.array, NULL);
	dstr_free(&filter_str);

	obs_properties_add_text(props, SETTING_COMMAND, TEXT_COMMAND, OBS_TEXT_DEFAULT);

	prop = obs_properties_add_bool(props, SETTING_DEBUG_ENABLED, TEXT_DEBUG_ENABLED);
	obs_property_set_modified_callback(prop,
			image_compare_debug_enabled_changed);
	prop = obs_properties_add_int(props, SETTING_DEBUG_IMAGE, TEXT_DEBUG_IMAGE, 1, MAX_IMAGES, 1);
	obs_property_set_visible(prop, false);

	UNUSED_PARAMETER(data);
	return props;
}

static void *image_compare_create(obs_data_t *settings, obs_source_t *context)
{
	struct image_compare_data *filter =
		bzalloc(sizeof(struct image_compare_data));
	char *effect_path;

	filter->context = context;

	effect_path = obs_module_file("image_compare.effect");
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(effect_path, NULL);
	filter->texrender = gs_texrender_create(GS_R8, GS_ZS_NONE);
	obs_leave_graphics();
	bfree(effect_path);

	obs_source_update(context, settings);

	return filter;
}

static void image_compare_destroy(void *data)
{
	struct image_compare_data *filter = data;

	image_compare_free(filter);

	obs_enter_graphics();
	gs_texrender_destroy(filter->texrender);
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();

	bfree(filter);
}

static void image_compare_tick(void *data, float t)
{
	struct image_compare_data *filter = data;
	bool enabled;

	enabled = obs_source_enabled(filter->context);
	if (enabled != filter->loaded) {
		if (enabled)
			image_compare_load(filter);
		else
			image_compare_unload(filter);
	}

	UNUSED_PARAMETER(t);
}

static void image_compare_render_video(struct image_compare_data *filter, gs_effect_t *effect, size_t offset, bool debug) {
	gs_eparam_t *param;

	if (!obs_source_process_filter_begin(filter->context, GS_RGBA,
				OBS_ALLOW_DIRECT_RENDERING))
		return;

	size_t count;
	if (debug) {
		count = 1;
	}  else {
		count = filter->count - offset;
		if (count > 7)
			count = 7;
	}

	param = gs_effect_get_param_by_name(filter->effect, "count");
	gs_effect_set_int(param, count);

	for (size_t i = 0; i < count; i++) {
		param = gs_effect_get_param_by_name(filter->effect, PARAMETER_NAMES[i]);
		gs_effect_set_texture(param, filter->image[i + offset].texture);

	}

	param = gs_effect_get_param_by_name(filter->effect, "debug");
	gs_effect_set_int(param, debug ? 0 : -1);

	obs_source_process_filter_end(filter->context, filter->effect, 0, 0);

	UNUSED_PARAMETER(effect);
}

static void *image_compare_command(void *param) {
	char *command = param;
	os_process_pipe_t *pipe;
	struct dstr output = {0};
	char data[128];

	blog(LOG_INFO, "Executing command: %s", command);
	pipe = os_process_pipe_create(command, "r");
	for (;;) {
		size_t len = os_process_pipe_read(pipe, (uint8_t*) data, 128);
		if (!len)
			break;
		dstr_ncat(&output, data, len);
	}
	os_process_pipe_destroy(pipe);
	blog(LOG_INFO, "Output: %s", output.array);
	dstr_free(&output);

	bfree(command);

	return NULL;
}

static void image_compare_texrender(struct image_compare_data *filter, gs_effect_t *effect) {
	uint32_t match = 0;

	for (size_t offset = 0; offset < filter->count; offset += 7) {
		uint8_t match_pass = 0xff;
		uint8_t *ptr;
		uint32_t linesize;
		gs_texture_t *texture;

		gs_texrender_reset(filter->texrender);
		if (gs_texrender_begin(filter->texrender, filter->cx, filter->cy)) {
			struct vec4 blank;

			vec4_zero(&blank);
			gs_clear(GS_CLEAR_COLOR, &blank, 0.0f, 0);
			gs_ortho(0.0f, (float) filter->cx, 0.0f, (float) filter->cy, -100.0f, 100.0f);

			image_compare_render_video(filter, effect, offset, false);

			gs_texrender_end(filter->texrender);
		} else {
			blog(LOG_ERROR, "gs_texrender_begin failed");
		}

		texture = gs_texrender_get_texture(filter->texrender);
		gs_stage_texture(filter->stagesurf, texture);
		if (gs_stagesurface_map(filter->stagesurf, &ptr, &linesize)) {
			for (uint32_t y = 0; y < filter->cy; y++) {
				for (uint32_t x = 0; x < filter->cx; x++) {
					match_pass = match_pass & ptr[y * linesize + x];
				}
			}

			gs_stagesurface_unmap(filter->stagesurf);
		} else {
			blog(LOG_ERROR, "gs_stagesurface_map failed");
		}

		match |= match_pass << offset;
	}

	uint32_t newMatch = match & ~filter->lastMatch;
	if (newMatch > 0) {
		for (size_t i = 0; i < filter->count; i++) {
			pthread_t t;
			struct dstr command = {0};

			if ((newMatch & 1 << i) == 0) {
				continue;
			}
			blog(LOG_INFO, "Now matching: %s",  filter->paths[i]);

			dstr_printf(&command, filter->command, i);
			pthread_create(&t, NULL, image_compare_command, dstr_to_mbs(&command));
			pthread_detach(t);
			dstr_free(&command);
		}
	}
	uint32_t oldMatch = ~match & filter->lastMatch;
	if (oldMatch > 0) {
		for (size_t i = 0; i < filter->count; i++) {
			if ((oldMatch & 1 << i) == 0) {
				continue;
			}
			blog(LOG_INFO, "No longer matching: %s", filter->paths[i]);
		}
	}
	filter->lastMatch = match;

	UNUSED_PARAMETER(effect);

}

static void image_compare_render(void *data, gs_effect_t *effect)
{
	struct image_compare_data *filter = data;

	if (!filter->loaded)
		return;

	image_compare_texrender(filter, effect);

	if (filter->debug >= 0) {
		image_compare_render_video(filter, effect, filter->debug, true);
	} else {
		obs_source_skip_video_filter(filter->context);
	}
}

struct obs_source_info image_compare = {
	.id                            = "image_compare",
	.type                          = OBS_SOURCE_TYPE_FILTER,
	.output_flags                  = OBS_SOURCE_VIDEO,
	.get_name                      = image_compare_get_name,
	.create                        = image_compare_create,
	.destroy                       = image_compare_destroy,
	.update                        = image_compare_update,
	.get_defaults                  = image_compare_defaults,
	.get_properties                = image_compare_properties,
	.video_tick                    = image_compare_tick,
	.video_render                  = image_compare_render,
};

bool obs_module_load(void)
{
	obs_register_source(&image_compare);
	return true;
}
