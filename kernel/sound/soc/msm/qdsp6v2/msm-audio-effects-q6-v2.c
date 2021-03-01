/* Copyright (c) 2013-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <sound/apr_audio-v2.h>
#include <sound/q6asm-v2.h>
#include <sound/compress_params.h>
#include <sound/msm-audio-effects-q6-v2.h>
#include <sound/msm-dts-eagle.h>
#include <sound/devdep_params.h>

#define MAX_ENABLE_CMD_SIZE 32

bool msm_audio_effects_is_effmodule_supp_in_top(int effect_module,
						int topology)
{
	switch (effect_module) {
	case VIRTUALIZER_MODULE:
	case REVERB_MODULE:
	case BASS_BOOST_MODULE:
	case EQ_MODULE:
		switch (topology) {
		case ASM_STREAM_POSTPROC_TOPO_ID_SA_PLUS:
		case ASM_STREAM_POSTPROC_TOPO_ID_HPX_PLUS:
		case ASM_STREAM_POSTPROC_TOPO_ID_HPX_MASTER:
			return true;
		default:
			return false;
		}
	case DTS_EAGLE_MODULE:
		switch (topology) {
		case ASM_STREAM_POSTPROC_TOPO_ID_DTS_HPX:
		case ASM_STREAM_POSTPROC_TOPO_ID_HPX_PLUS:
		case ASM_STREAM_POSTPROC_TOPO_ID_HPX_MASTER:
			return true;
		default:
			return false;
		}
	case SOFT_VOLUME2_MODULE:
	case DTS_EAGLE_MODULE_ENABLE:
		switch (topology) {
		case ASM_STREAM_POSTPROC_TOPO_ID_HPX_PLUS:
		case ASM_STREAM_POSTPROC_TOPO_ID_HPX_MASTER:
			return true;
		default:
			return false;
		}
	default:
		return false;
	}
}

int msm_audio_effects_enable_extn(struct audio_client *ac,
				struct msm_nt_eff_all_config *effects,
				bool flag)
{
	uint32_t updt_params[MAX_ENABLE_CMD_SIZE] = {0};
	uint32_t params_length;
	int rc = 0;

	pr_debug("%s\n", __func__);
	if (!ac) {
		pr_err("%s: cannot set audio effects\n", __func__);
		return -EINVAL;
	}
	params_length = 0;
	updt_params[0] = AUDPROC_MODULE_ID_VIRTUALIZER;
	updt_params[1] = AUDPROC_PARAM_ID_ENABLE;
	updt_params[2] = VIRTUALIZER_ENABLE_PARAM_SZ;
	updt_params[3] = flag;
	params_length += COMMAND_PAYLOAD_SZ + VIRTUALIZER_ENABLE_PARAM_SZ;
	if (effects->virtualizer.enable_flag)
		q6asm_send_audio_effects_params(ac, (char *)&updt_params[0],
					params_length);
	memset(updt_params, 0, MAX_ENABLE_CMD_SIZE);
	params_length = 0;
	updt_params[0] = AUDPROC_MODULE_ID_BASS_BOOST;
	updt_params[1] = AUDPROC_PARAM_ID_ENABLE;
	updt_params[2] = BASS_BOOST_ENABLE_PARAM_SZ;
	updt_params[3] = flag;
	params_length += COMMAND_PAYLOAD_SZ + BASS_BOOST_ENABLE_PARAM_SZ;
	if (effects->bass_boost.enable_flag)
		q6asm_send_audio_effects_params(ac, (char *)&updt_params[0],
					params_length);
	memset(updt_params, 0, MAX_ENABLE_CMD_SIZE);
	params_length = 0;
	updt_params[0] = AUDPROC_MODULE_ID_POPLESS_EQUALIZER;
	updt_params[1] = AUDPROC_PARAM_ID_ENABLE;
	updt_params[2] = EQ_ENABLE_PARAM_SZ;
	updt_params[3] = flag;
	params_length += COMMAND_PAYLOAD_SZ + EQ_ENABLE_PARAM_SZ;
	if (effects->equalizer.enable_flag)
		q6asm_send_audio_effects_params(ac, (char *)&updt_params[0],
					params_length);
	return rc;
}

int msm_audio_effects_virtualizer_handler(struct audio_client *ac,
				struct virtualizer_params *virtualizer,
				long *values)
{
	int devices = *values++;
	int num_commands = *values++;
	char *params;
	int *updt_params, i, prev_enable_flag;
	uint32_t params_length = (MAX_INBAND_PARAM_SZ);
	int rc = 0;

	pr_debug("%s\n", __func__);
	if (!ac) {
		pr_err("%s: cannot set audio effects\n", __func__);
		return -EINVAL;
	}
	params = kzalloc(params_length, GFP_KERNEL);
	if (!params) {
		pr_err("%s, params memory alloc failed\n", __func__);
		return -ENOMEM;
	}
	pr_debug("%s: device: %d\n", __func__, devices);
	updt_params = (int *)params;
	params_length = 0;
	for (i = 0; i < num_commands; i++) {
		uint32_t command_id = *values++;
		uint32_t command_config_state = *values++;
		uint32_t index_offset = *values++;
		uint32_t length = *values++;
		switch (command_id) {
		case VIRTUALIZER_ENABLE:
			if (length != 1 || index_offset != 0) {
				pr_err("VIRT ENABLE:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			prev_enable_flag = virtualizer->enable_flag;
			virtualizer->enable_flag = *values++;
			pr_debug("%s:VIRT ENABLE prev:%d, new:%d\n", __func__,
				prev_enable_flag, virtualizer->enable_flag);
			if (prev_enable_flag != virtualizer->enable_flag) {
				*updt_params++ = AUDPROC_MODULE_ID_VIRTUALIZER;
				*updt_params++ =
					AUDPROC_PARAM_ID_VIRTUALIZER_ENABLE;
				*updt_params++ = VIRTUALIZER_ENABLE_PARAM_SZ;
				*updt_params++ = virtualizer->enable_flag;
				params_length += COMMAND_PAYLOAD_SZ +
					VIRTUALIZER_ENABLE_PARAM_SZ;
			}
			break;
		case VIRTUALIZER_STRENGTH:
			if (length != 1 || index_offset != 0) {
				pr_err("VIRT STRENGTH:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			virtualizer->strength = *values++;
			pr_debug("%s: VIRT STRENGTH val: %d\n",
					__func__, virtualizer->strength);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_VIRTUALIZER;
				*updt_params++ =
					AUDPROC_PARAM_ID_VIRTUALIZER_STRENGTH;
				*updt_params++ = VIRTUALIZER_STRENGTH_PARAM_SZ;
				*updt_params++ = virtualizer->strength;
				params_length += COMMAND_PAYLOAD_SZ +
					VIRTUALIZER_STRENGTH_PARAM_SZ;
			}
			break;
		case VIRTUALIZER_OUT_TYPE:
			if (length != 1 || index_offset != 0) {
				pr_err("VIRT OUT_TYPE:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			virtualizer->out_type = *values++;
			pr_debug("%s: VIRT OUT_TYPE val:%d\n",
				__func__, virtualizer->out_type);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_VIRTUALIZER;
				*updt_params++ =
					AUDPROC_PARAM_ID_VIRTUALIZER_OUT_TYPE;
				*updt_params++ = VIRTUALIZER_OUT_TYPE_PARAM_SZ;
				*updt_params++ = virtualizer->out_type;
				params_length += COMMAND_PAYLOAD_SZ +
					VIRTUALIZER_OUT_TYPE_PARAM_SZ;
			}
			break;
		case VIRTUALIZER_GAIN_ADJUST:
			if (length != 1 || index_offset != 0) {
				pr_err("VIRT GAIN_ADJUST: invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			virtualizer->gain_adjust = *values++;
			pr_debug("%s: VIRT GAIN_ADJUST val:%d\n",
				__func__, virtualizer->gain_adjust);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_VIRTUALIZER;
				*updt_params++ =
				       AUDPROC_PARAM_ID_VIRTUALIZER_GAIN_ADJUST;
				*updt_params++ =
					VIRTUALIZER_GAIN_ADJUST_PARAM_SZ;
				*updt_params++ = virtualizer->gain_adjust;
				params_length += COMMAND_PAYLOAD_SZ +
					VIRTUALIZER_GAIN_ADJUST_PARAM_SZ;
			}
			break;
		default:
			pr_err("%s: Invalid command to set config\n", __func__);
			break;
		}
	}
	if (params_length && !msm_dts_eagle_is_hpx_on())
		q6asm_send_audio_effects_params(ac, params,
						params_length);
	else
		pr_debug("%s: did not send pp params\n", __func__);
invalid_config:
	kfree(params);
	return rc;
}

int msm_audio_effects_reverb_handler(struct audio_client *ac,
				     struct reverb_params *reverb,
				     long *values)
{
	int devices = *values++;
	int num_commands = *values++;
	char *params;
	int *updt_params, i, prev_enable_flag;
	uint32_t params_length = (MAX_INBAND_PARAM_SZ);
	int rc = 0;

	pr_debug("%s\n", __func__);
	if (!ac) {
		pr_err("%s: cannot set audio effects\n", __func__);
		return -EINVAL;
	}
	params = kzalloc(params_length, GFP_KERNEL);
	if (!params) {
		pr_err("%s, params memory alloc failed\n", __func__);
		return -ENOMEM;
	}
	pr_debug("%s: device: %d\n", __func__, devices);
	updt_params = (int *)params;
	params_length = 0;
	for (i = 0; i < num_commands; i++) {
		uint32_t command_id = *values++;
		uint32_t command_config_state = *values++;
		uint32_t index_offset = *values++;
		uint32_t length = *values++;
		switch (command_id) {
		case REVERB_ENABLE:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_ENABLE:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			prev_enable_flag = reverb->enable_flag;
			reverb->enable_flag = *values++;
			pr_debug("%s:REVERB_ENABLE prev:%d,new:%d\n", __func__,
					prev_enable_flag, reverb->enable_flag);
			if (prev_enable_flag != reverb->enable_flag) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ = AUDPROC_PARAM_ID_REVERB_ENABLE;
				*updt_params++ = REVERB_ENABLE_PARAM_SZ;
				*updt_params++ = reverb->enable_flag;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_ENABLE_PARAM_SZ;
			}
			break;
		case REVERB_MODE:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_MODE:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->mode = *values++;
			pr_debug("%s: REVERB_MODE val:%d\n",
				__func__, reverb->mode);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ = AUDPROC_PARAM_ID_REVERB_MODE;
				*updt_params++ = REVERB_MODE_PARAM_SZ;
				*updt_params++ = reverb->mode;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_MODE_PARAM_SZ;
			}
			break;
		case REVERB_PRESET:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_PRESET:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->preset = *values++;
			pr_debug("%s: REVERB_PRESET val:%d\n",
					__func__, reverb->preset);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ = AUDPROC_PARAM_ID_REVERB_PRESET;
				*updt_params++ = REVERB_PRESET_PARAM_SZ;
				*updt_params++ = reverb->preset;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_PRESET_PARAM_SZ;
			}
			break;
		case REVERB_WET_MIX:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_WET_MIX:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->wet_mix = *values++;
			pr_debug("%s: REVERB_WET_MIX val:%d\n",
				__func__, reverb->wet_mix);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ =
					AUDPROC_PARAM_ID_REVERB_WET_MIX;
				*updt_params++ = REVERB_WET_MIX_PARAM_SZ;
				*updt_params++ = reverb->wet_mix;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_WET_MIX_PARAM_SZ;
			}
			break;
		case REVERB_GAIN_ADJUST:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_GAIN_ADJUST:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->gain_adjust = *values++;
			pr_debug("%s: REVERB_GAIN_ADJUST val:%d\n",
					__func__, reverb->gain_adjust);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ =
					AUDPROC_PARAM_ID_REVERB_GAIN_ADJUST;
				*updt_params++ = REVERB_GAIN_ADJUST_PARAM_SZ;
				*updt_params++ = reverb->gain_adjust;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_GAIN_ADJUST_PARAM_SZ;
			}
			break;
		case REVERB_ROOM_LEVEL:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_ROOM_LEVEL:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->room_level = *values++;
			pr_debug("%s: REVERB_ROOM_LEVEL val:%d\n",
				__func__, reverb->room_level);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ =
					AUDPROC_PARAM_ID_REVERB_ROOM_LEVEL;
				*updt_params++ = REVERB_ROOM_LEVEL_PARAM_SZ;
				*updt_params++ = reverb->room_level;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_ROOM_LEVEL_PARAM_SZ;
			}
			break;
		case REVERB_ROOM_HF_LEVEL:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_ROOM_HF_LEVEL:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->room_hf_level = *values++;
			pr_debug("%s: REVERB_ROOM_HF_LEVEL val%d\n",
				__func__, reverb->room_hf_level);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ =
					AUDPROC_PARAM_ID_REVERB_ROOM_HF_LEVEL;
				*updt_params++ = REVERB_ROOM_HF_LEVEL_PARAM_SZ;
				*updt_params++ = reverb->room_hf_level;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_ROOM_HF_LEVEL_PARAM_SZ;
			}
			break;
		case REVERB_DECAY_TIME:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_DECAY_TIME:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->decay_time = *values++;
			pr_debug("%s: REVERB_DECAY_TIME val:%d\n",
				__func__, reverb->decay_time);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ =
					AUDPROC_PARAM_ID_REVERB_DECAY_TIME;
				*updt_params++ = REVERB_DECAY_TIME_PARAM_SZ;
				*updt_params++ = reverb->decay_time;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_DECAY_TIME_PARAM_SZ;
			}
			break;
		case REVERB_DECAY_HF_RATIO:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_DECAY_HF_RATIOinvalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->decay_hf_ratio = *values++;
			pr_debug("%s: REVERB_DECAY_HF_RATIO val%d\n",
				__func__, reverb->decay_hf_ratio);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ =
					AUDPROC_PARAM_ID_REVERB_DECAY_HF_RATIO;
				*updt_params++ = REVERB_DECAY_HF_RATIO_PARAM_SZ;
				*updt_params++ = reverb->decay_hf_ratio;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_DECAY_HF_RATIO_PARAM_SZ;
			}
			break;
		case REVERB_REFLECTIONS_LEVEL:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_REFLECTION_LVLinvalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->reflections_level = *values++;
			pr_debug("%s: REVERB_REFLECTIONS_LEVEL val:%d\n",
				__func__, reverb->reflections_level);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ =
				      AUDPROC_PARAM_ID_REVERB_REFLECTIONS_LEVEL;
				*updt_params++ =
					REVERB_REFLECTIONS_LEVEL_PARAM_SZ;
				*updt_params++ = reverb->reflections_level;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_REFLECTIONS_LEVEL_PARAM_SZ;
			}
			break;
		case REVERB_REFLECTIONS_DELAY:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_REFLECTION_DLYinvalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->reflections_delay = *values++;
			pr_debug("%s: REVERB_REFLECTIONS_DELAY val:%d\n",
				__func__, reverb->reflections_delay);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ =
				      AUDPROC_PARAM_ID_REVERB_REFLECTIONS_DELAY;
				*updt_params++ =
					REVERB_REFLECTIONS_DELAY_PARAM_SZ;
				*updt_params++ = reverb->reflections_delay;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_REFLECTIONS_DELAY_PARAM_SZ;
			}
			break;
		case REVERB_LEVEL:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_LEVEL:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->level = *values++;
			pr_debug("%s: REVERB_LEVEL val:%d\n",
				__func__, reverb->level);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ = AUDPROC_PARAM_ID_REVERB_LEVEL;
				*updt_params++ = REVERB_LEVEL_PARAM_SZ;
				*updt_params++ = reverb->level;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_LEVEL_PARAM_SZ;
			}
			break;
		case REVERB_DELAY:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_DELAY:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->delay = *values++;
			pr_debug("%s:REVERB_DELAY val:%d\n",
					__func__, reverb->delay);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ = AUDPROC_PARAM_ID_REVERB_DELAY;
				*updt_params++ = REVERB_DELAY_PARAM_SZ;
				*updt_params++ = reverb->delay;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_DELAY_PARAM_SZ;
			}
			break;
		case REVERB_DIFFUSION:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_DIFFUSION:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->diffusion = *values++;
			pr_debug("%s: REVERB_DIFFUSION val:%d\n",
				__func__, reverb->diffusion);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ =
					AUDPROC_PARAM_ID_REVERB_DIFFUSION;
				*updt_params++ = REVERB_DIFFUSION_PARAM_SZ;
				*updt_params++ = reverb->diffusion;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_DIFFUSION_PARAM_SZ;
			}
			break;
		case REVERB_DENSITY:
			if (length != 1 || index_offset != 0) {
				pr_err("REVERB_DENSITY:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			reverb->density = *values++;
			pr_debug("%s: REVERB_DENSITY val:%d\n",
				__func__, reverb->density);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_REVERB;
				*updt_params++ =
					AUDPROC_PARAM_ID_REVERB_DENSITY;
				*updt_params++ = REVERB_DENSITY_PARAM_SZ;
				*updt_params++ = reverb->density;
				params_length += COMMAND_PAYLOAD_SZ +
					REVERB_DENSITY_PARAM_SZ;
			}
			break;
		default:
			pr_err("%s: Invalid command to set config\n", __func__);
			break;
		}
	}
	if (params_length && !msm_dts_eagle_is_hpx_on())
		q6asm_send_audio_effects_params(ac, params,
						params_length);
	else
		pr_debug("%s: did not send pp params\n", __func__);
invalid_config:
	kfree(params);
	return rc;
}

int msm_audio_effects_bass_boost_handler(struct audio_client *ac,
					struct bass_boost_params *bass_boost,
					long *values)
{
	int devices = *values++;
	int num_commands = *values++;
	char *params;
	int *updt_params, i, prev_enable_flag;
	uint32_t params_length = (MAX_INBAND_PARAM_SZ);
	int rc = 0;

	pr_debug("%s\n", __func__);
	if (!ac) {
		pr_err("%s: cannot set audio effects\n", __func__);
		return -EINVAL;
	}
	params = kzalloc(params_length, GFP_KERNEL);
	if (!params) {
		pr_err("%s, params memory alloc failed\n", __func__);
		return -ENOMEM;
	}
	pr_debug("%s: device: %d\n", __func__, devices);
	updt_params = (int *)params;
	params_length = 0;
	for (i = 0; i < num_commands; i++) {
		uint32_t command_id = *values++;
		uint32_t command_config_state = *values++;
		uint32_t index_offset = *values++;
		uint32_t length = *values++;
		switch (command_id) {
		case BASS_BOOST_ENABLE:
			if (length != 1 || index_offset != 0) {
				pr_err("BASS_BOOST_ENABLE:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			prev_enable_flag = bass_boost->enable_flag;
			bass_boost->enable_flag = *values++;
			pr_debug("%s: BASS_BOOST_ENABLE prev:%d new:%d\n",
				__func__, prev_enable_flag,
				bass_boost->enable_flag);
			if (prev_enable_flag != bass_boost->enable_flag) {
				*updt_params++ = AUDPROC_MODULE_ID_BASS_BOOST;
				*updt_params++ =
					AUDPROC_PARAM_ID_BASS_BOOST_ENABLE;
				*updt_params++ = BASS_BOOST_ENABLE_PARAM_SZ;
				*updt_params++ = bass_boost->enable_flag;
				params_length += COMMAND_PAYLOAD_SZ +
					BASS_BOOST_ENABLE_PARAM_SZ;
			}
			break;
		case BASS_BOOST_MODE:
			if (length != 1 || index_offset != 0) {
				pr_err("BASS_BOOST_MODE:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			bass_boost->mode = *values++;
			pr_debug("%s: BASS_BOOST_MODE val:%d\n",
				__func__, bass_boost->mode);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_BASS_BOOST;
				*updt_params++ =
					AUDPROC_PARAM_ID_BASS_BOOST_MODE;
				*updt_params++ = BASS_BOOST_MODE_PARAM_SZ;
				*updt_params++ = bass_boost->mode;
				params_length += COMMAND_PAYLOAD_SZ +
					BASS_BOOST_MODE_PARAM_SZ;
			}
			break;
		case BASS_BOOST_STRENGTH:
			if (length != 1 || index_offset != 0) {
				pr_err("BASS_BOOST_STRENGTH:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			bass_boost->strength = *values++;
			pr_debug("%s: BASS_BOOST_STRENGTHi val:%d\n",
				__func__, bass_boost->strength);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ = AUDPROC_MODULE_ID_BASS_BOOST;
				*updt_params++ =
					AUDPROC_PARAM_ID_BASS_BOOST_STRENGTH;
				*updt_params++ = BASS_BOOST_STRENGTH_PARAM_SZ;
				*updt_params++ = bass_boost->strength;
				params_length += COMMAND_PAYLOAD_SZ +
					BASS_BOOST_STRENGTH_PARAM_SZ;
			}
			break;
		default:
			pr_err("%s: Invalid command to set config\n", __func__);
			break;
		}
	}
	if (params_length && !msm_dts_eagle_is_hpx_on())
		q6asm_send_audio_effects_params(ac, params,
						params_length);
	else
		pr_debug("%s: did not send pp params\n", __func__);
invalid_config:
	kfree(params);
	return rc;
}

int msm_audio_effects_popless_eq_handler(struct audio_client *ac,
					 struct eq_params *eq,
					 long *values)
{
	int devices = *values++;
	int num_commands = *values++;
	char *params;
	int *updt_params, i, prev_enable_flag;
	uint32_t params_length = (MAX_INBAND_PARAM_SZ);
	int rc = 0;

	pr_debug("%s\n", __func__);
	if (!ac) {
		pr_err("%s: cannot set audio effects\n", __func__);
		return -EINVAL;
	}
	params = kzalloc(params_length, GFP_KERNEL);
	if (!params) {
		pr_err("%s, params memory alloc failed\n", __func__);
		return -ENOMEM;
	}
	pr_debug("%s: device: %d\n", __func__, devices);
	updt_params = (int *)params;
	params_length = 0;
	for (i = 0; i < num_commands; i++) {
		uint32_t command_id = *values++;
		uint32_t command_config_state = *values++;
		uint32_t index_offset = *values++;
		uint32_t length = *values++;
		int idx, j;
		switch (command_id) {
		case EQ_ENABLE:
			if (length != 1 || index_offset != 0) {
				pr_err("EQ_ENABLE:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			prev_enable_flag = eq->enable_flag;
			eq->enable_flag = *values++;
			pr_debug("%s: EQ_ENABLE prev:%d new:%d\n", __func__,
				prev_enable_flag, eq->enable_flag);
			if (prev_enable_flag != eq->enable_flag) {
				*updt_params++ =
					AUDPROC_MODULE_ID_POPLESS_EQUALIZER;
				*updt_params++ = AUDPROC_PARAM_ID_EQ_ENABLE;
				*updt_params++ = EQ_ENABLE_PARAM_SZ;
				*updt_params++ = eq->enable_flag;
				params_length += COMMAND_PAYLOAD_SZ +
					EQ_ENABLE_PARAM_SZ;
			}
			break;
		case EQ_CONFIG:
			if (length < EQ_CONFIG_PARAM_LEN || index_offset != 0) {
				pr_err("EQ_CONFIG:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			pr_debug("%s: EQ_CONFIG bands:%d, pgain:%d, pset:%d\n",
				 __func__, eq->config.num_bands,
				eq->config.eq_pregain, eq->config.preset_id);
			for (idx = 0; idx < MAX_EQ_BANDS; idx++)
				eq->per_band_cfg[idx].band_idx = -1;
			eq->config.eq_pregain = *values++;
			eq->config.preset_id = *values++;
			eq->config.num_bands = *values++;
			if (eq->config.num_bands > MAX_EQ_BANDS) {
				pr_err("EQ_CONFIG:invalid num of bands\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			if (eq->config.num_bands &&
			    (((length - EQ_CONFIG_PARAM_LEN)/
				EQ_CONFIG_PER_BAND_PARAM_LEN)
				!= eq->config.num_bands)) {
				pr_err("EQ_CONFIG:invalid length per band\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			for (j = 0; j < eq->config.num_bands; j++) {
				idx = *values++;
				if (idx >= MAX_EQ_BANDS) {
					pr_err("EQ_CONFIG:invalid band index\n");
					rc = -EINVAL;
					goto invalid_config;
				}
				eq->per_band_cfg[idx].band_idx = idx;
				eq->per_band_cfg[idx].filter_type = *values++;
				eq->per_band_cfg[idx].freq_millihertz =
								*values++;
				eq->per_band_cfg[idx].gain_millibels =
								*values++;
				eq->per_band_cfg[idx].quality_factor =
								*values++;
			}
			if (command_config_state == CONFIG_SET) {
				int config_param_length = EQ_CONFIG_PARAM_SZ +
					(EQ_CONFIG_PER_BAND_PARAM_SZ*
					 eq->config.num_bands);
				*updt_params++ =
					AUDPROC_MODULE_ID_POPLESS_EQUALIZER;
				*updt_params++ = AUDPROC_PARAM_ID_EQ_CONFIG;
				*updt_params++ = config_param_length;
				*updt_params++ = eq->config.eq_pregain;
				*updt_params++ = eq->config.preset_id;
				*updt_params++ = eq->config.num_bands;
				for (idx = 0; idx < MAX_EQ_BANDS; idx++) {
					if (eq->per_band_cfg[idx].band_idx < 0)
						continue;
					*updt_params++ =
					  eq->per_band_cfg[idx].filter_type;
					*updt_params++ =
					  eq->per_band_cfg[idx].freq_millihertz;
					*updt_params++ =
					  eq->per_band_cfg[idx].gain_millibels;
					*updt_params++ =
					  eq->per_band_cfg[idx].quality_factor;
					*updt_params++ =
					  eq->per_band_cfg[idx].band_idx;
				}
				params_length += COMMAND_PAYLOAD_SZ +
						config_param_length;
			}
			break;
		case EQ_BAND_INDEX:
			if (length != 1 || index_offset != 0) {
				pr_err("EQ_BAND_INDEX:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			idx = *values++;
			if (idx > MAX_EQ_BANDS) {
				pr_err("EQ_BAND_INDEX:invalid band index\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			eq->band_index = idx;
			pr_debug("%s: EQ_BAND_INDEX val:%d\n",
				__func__, eq->band_index);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ =
					AUDPROC_MODULE_ID_POPLESS_EQUALIZER;
				*updt_params++ =
					AUDPROC_PARAM_ID_EQ_BAND_INDEX;
				*updt_params++ = EQ_BAND_INDEX_PARAM_SZ;
				*updt_params++ = eq->band_index;
				params_length += COMMAND_PAYLOAD_SZ +
					EQ_BAND_INDEX_PARAM_SZ;
			}
			break;
		case EQ_SINGLE_BAND_FREQ:
			if (length != 1 || index_offset != 0) {
				pr_err("EQ_SINGLE_BAND_FREQ:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			if (eq->band_index > MAX_EQ_BANDS) {
				pr_err("EQ_SINGLE_BAND_FREQ:invalid index\n");
				break;
			}
			eq->freq_millihertz = *values++;
			pr_debug("%s: EQ_SINGLE_BAND_FREQ idx:%d, val:%d\n",
				__func__, eq->band_index, eq->freq_millihertz);
			if (command_config_state == CONFIG_SET) {
				*updt_params++ =
					AUDPROC_MODULE_ID_POPLESS_EQUALIZER;
				*updt_params++ =
					AUDPROC_PARAM_ID_EQ_SINGLE_BAND_FREQ;
				*updt_params++ = EQ_SINGLE_BAND_FREQ_PARAM_SZ;
				*updt_params++ = eq->freq_millihertz;
				params_length += COMMAND_PAYLOAD_SZ +
					EQ_SINGLE_BAND_FREQ_PARAM_SZ;
			}
			break;
		default:
			pr_err("%s: Invalid command to set config\n", __func__);
			break;
		}
	}
	if (params_length && !msm_dts_eagle_is_hpx_on())
		q6asm_send_audio_effects_params(ac, params,
						params_length);
	else
		pr_debug("%s: did not send pp params\n", __func__);
invalid_config:
	kfree(params);
	return rc;
}

static int __msm_audio_effects_volume_handler(struct audio_client *ac,
					      struct soft_volume_params *vol,
					      long *values,
					      int instance)
{
	int devices;
	int num_commands;
	char *params;
	int *updt_params, i;
	uint32_t params_length = (MAX_INBAND_PARAM_SZ);
	int rc = 0;

	pr_debug("%s: instance: %d\n", __func__, instance);
	if (!values) {
		pr_err("%s: set audio effects failed, no valid data\n",
			__func__);
		return -EINVAL;
	}
	if (!ac) {
		pr_err("%s: cannot set audio effects as audio client is NULL\n",
			__func__);
		return -EINVAL;
	}
	params = kzalloc(params_length, GFP_KERNEL);
	if (!params) {
		pr_err("%s, params memory alloc failed\n", __func__);
		return -ENOMEM;
	}
	devices = *values++;
	num_commands = *values++;
	updt_params = (int *)params;
	params_length = 0;
	for (i = 0; i < num_commands; i++) {
		uint32_t command_id = *values++;
		uint32_t command_config_state = *values++;
		uint32_t index_offset = *values++;
		uint32_t length = *values++;
		switch (command_id) {
		case SOFT_VOLUME_GAIN_2CH:
		case SOFT_VOLUME2_GAIN_2CH:
			if (length != 2 || index_offset != 0) {
				pr_err("VOLUME_GAIN_2CH/VOLUME2_GAIN_2CH:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			vol->left_gain = *values++;
			vol->right_gain = *values++;
			vol->master_gain = 0x2000;
			if (command_config_state == CONFIG_SET) {
				if (instance == SOFT_VOLUME_INSTANCE_2)
					*updt_params++ =
							ASM_MODULE_ID_VOL_CTRL2;
				else
					*updt_params++ = ASM_MODULE_ID_VOL_CTRL;
				*updt_params++ =
					ASM_PARAM_ID_VOL_CTRL_LR_CHANNEL_GAIN;
				*updt_params++ = SOFT_VOLUME_GAIN_2CH_PARAM_SZ;
				*updt_params++ = (vol->left_gain << 16) |
						 vol->right_gain;
				params_length += COMMAND_PAYLOAD_SZ +
						SOFT_VOLUME_GAIN_2CH_PARAM_SZ;
				if (instance == SOFT_VOLUME_INSTANCE_2)
					*updt_params++ =
							ASM_MODULE_ID_VOL_CTRL2;
				else
					*updt_params++ = ASM_MODULE_ID_VOL_CTRL;
				*updt_params++ =
					ASM_PARAM_ID_VOL_CTRL_MASTER_GAIN;
				*updt_params++ =
					SOFT_VOLUME_GAIN_MASTER_PARAM_SZ;
				*updt_params++ = vol->master_gain;
				params_length += COMMAND_PAYLOAD_SZ +
					SOFT_VOLUME_GAIN_MASTER_PARAM_SZ;
			}
			break;
		case SOFT_VOLUME_GAIN_MASTER:
		case SOFT_VOLUME2_GAIN_MASTER:
			if (length != 1 || index_offset != 0) {
				pr_err("VOLUME_GAIN_MASTER/VOLUME2_GAIN_MASTER:invalid params\n");
				rc = -EINVAL;
				goto invalid_config;
			}
			vol->left_gain = 0x2000;
			vol->right_gain = 0x2000;
			vol->master_gain = *values++;
			if (command_config_state == CONFIG_SET) {
				if (instance == SOFT_VOLUME_INSTANCE_2)
					*updt_params++ =
							ASM_MODULE_ID_VOL_CTRL2;
				else
					*updt_params++ = ASM_MODULE_ID_VOL_CTRL;
				*updt_params++ =
					ASM_PARAM_ID_VOL_CTRL_LR_CHANNEL_GAIN;
				*updt_params++ = SOFT_VOLUME_GAIN_2CH_PARAM_SZ;
				*updt_params++ = (vol->left_gain << 16) |
						 vol->right_gain;
				params_length += COMMAND_PAYLOAD_SZ +
						SOFT_VOLUME_GAIN_2CH_PARAM_SZ;
				if (instance == SOFT_VOLUME_INSTANCE_2)
					*updt_params++ =
							ASM_MODULE_ID_VOL_CTRL2;
				else
					*updt_params++ = ASM_MODULE_ID_VOL_CTRL;
				*updt_params++ =
					ASM_PARAM_ID_VOL_CTRL_MASTER_GAIN;
				*updt_params++ =
					SOFT_VOLUME_GAIN_MASTER_PARAM_SZ;
				*updt_params++ = vol->master_gain;
				params_length += COMMAND_PAYLOAD_SZ +
					SOFT_VOLUME_GAIN_MASTER_PARAM_SZ;
			}
			break;
		default:
			pr_err("%s: Invalid command id: %d to set config\n",
				__func__, command_id);
			break;
		}
	}
	if (params_length)
		q6asm_send_audio_effects_params(ac, params,
						params_length);
invalid_config:
	kfree(params);
	return rc;
}

int msm_audio_effects_volume_handler(struct audio_client *ac,
				     struct soft_volume_params *vol,
				     long *values)
{
	return __msm_audio_effects_volume_handler(ac, vol, values,
						  SOFT_VOLUME_INSTANCE_1);
}

int msm_audio_effects_volume_handler_v2(struct audio_client *ac,
					struct soft_volume_params *vol,
					long *values, int instance)
{
	return __msm_audio_effects_volume_handler(ac, vol, values, instance);
}
