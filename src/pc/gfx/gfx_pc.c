#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifdef __SSE__
#include <xmmintrin.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#ifndef _LANGUAGE_C
#define _LANGUAGE_C
#endif
#include <PR/gbi.h>

#include "config.h"
#include "macros.h"

#include "engine/lighting_engine.h"
#include "engine/math_util.h"

#include "game/object_helpers.h"
#include "game/rendering_graph_node.h"

#include "pc/configfile.h"
#include "pc/debug_context.h"
#include "pc/pc_main.h"
#include "pc/platform.h"

#include "pc/fs/fs.h"

#include "pc/gfx/gfx_cc.h"
#include "pc/gfx/gfx_pc.h"
#include "pc/gfx/gfx_rendering_api.h"
#include "pc/gfx/gfx_screen_config.h"
#include "pc/gfx/gfx_window_manager_api.h"

// this is used for multi-textures
// and it's quite a hack... instead of allowing 8 tiles, we basically only allow 2
#define G_TX_LOADTILE_6_UNKNOWN 6
//////////////////////////////////

#define RDP_TILES 2

u8 gGfxPcResetTex1 = 0;

static struct TextureCache gfx_texture_cache = { 0 };
static struct ColorCombiner color_combiner_pool[CC_MAX_SHADERS] = { 0 };
static uint8_t color_combiner_pool_size = 0;
static uint8_t color_combiner_pool_index = 0;

static struct RSP {
    ALIGNED16 Mat4 MP_matrix;
    ALIGNED16 Mat4 P_matrix;
    ALIGNED16 Mat4 modelview_matrix_stack[MAX_MATRIX_STACK_SIZE];
    uint32_t modelview_matrix_stack_size;
    
    uint32_t geometry_mode;
    int16_t fog_mul, fog_offset;
    
    struct {
        // U0.16
        uint16_t s, t;
    } texture_scaling_factor;
    
    bool lights_changed;
    uint8_t current_num_lights; // includes ambient light
    Vec3f current_lights_coeffs[MAX_LIGHTS];
    Vec3f current_lookat_coeffs[2]; // lookat_x, lookat_y
    Light_t current_lights[MAX_LIGHTS + 1];

    struct GfxVertex loaded_vertices[MAX_VERTICES + 4];
} rsp;

static struct RDP {
    const uint8_t *palette;
    struct UnloadedTex texture_to_load;
    struct TextureTile texture_tile;
    struct GfxTexture loaded_texture[RDP_TILES];
    bool textures_changed[RDP_TILES];

    uint32_t other_mode_l, other_mode_h;
    struct CombineMode combine_mode;

    struct RGBA env_color, prim_color, fog_color, fill_color;
    struct Box viewport, scissor;
    bool viewport_or_scissor_changed;
    void *z_buf_address;
    void *color_image_address;
} rdp;

static struct RenderingState {
    bool depth_test;
    bool depth_mask;
    bool decal_mode;
    bool alpha_blend;
    struct Box viewport, scissor;
    struct ShaderProgram *shader_program;
    struct TextureHashmapNode *textures[2];
} rendering_state;

struct GfxDimensions gfx_current_dimensions = { 0 };

static bool dropped_frame = false;

static float buf_vbo[MAX_BUFFERED * (26 * 3)] = { 0.0f }; // 3 vertices in a triangle and 26 floats per vtx
static size_t buf_vbo_len = 0;
static size_t buf_vbo_num_tris = 0;

static struct GfxWindowManagerAPI *gfx_wapi = NULL;
static struct GfxRenderingAPI *gfx_rapi = NULL;

static f32 sDepthZAdd = 0;
static f32 sDepthZMult = 1;
static f32 sDepthZSub = 0;

Vec3f gLightingDir = { 0.0f, 0.0f, 0.0f };
Color gLightingColor[2] = { { 0xFF, 0xFF, 0xFF }, { 0xFF, 0xFF, 0xFF } };
Color gVertexColor = { 0xFF, 0xFF, 0xFF };
Color gFogColor = { 0xFF, 0xFF, 0xFF };
f32 gFogIntensity = 1;

// 4x4 pink-black checkerboard texture to indicate missing textures
#define MISSING_W 4
#define MISSING_H 4
static const uint8_t missing_texture[MISSING_W * MISSING_H * 4] = {
    0xFF, 0x00, 0xFF, 0xFF,  0xFF, 0x00, 0xFF, 0xFF,  0x00, 0x00, 0x00, 0xFF,  0x00, 0x00, 0x00, 0xFF,
    0xFF, 0x00, 0xFF, 0xFF,  0xFF, 0x00, 0xFF, 0xFF,  0x00, 0x00, 0x00, 0xFF,  0x00, 0x00, 0x00, 0xFF,
    0x00, 0x00, 0x00, 0xFF,  0x00, 0x00, 0x00, 0xFF,  0xFF, 0x00, 0xFF, 0xFF,  0xFF, 0x00, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0xFF,  0x00, 0x00, 0x00, 0xFF,  0xFF, 0x00, 0xFF, 0xFF,  0xFF, 0x00, 0xFF, 0xFF,
};

static bool sOnlyTextureChangeOnAddrChange = false;

static void gfx_update_loaded_texture(uint8_t tile_number, uint32_t size_bytes, const uint8_t* addr) {
    if (tile_number >= RDP_TILES) { return; }
    if (!sOnlyTextureChangeOnAddrChange) {
        rdp.textures_changed[tile_number] = true;
    } else if (!rdp.textures_changed[tile_number]) {
        rdp.textures_changed[tile_number] = rdp.loaded_texture[tile_number].addr != addr;
    }
    rdp.loaded_texture[tile_number].size_bytes = size_bytes;
    rdp.loaded_texture[tile_number].addr = addr;
}

//////////////////////////
// forward declaration //
////////////////////////
void ext_gfx_run_dl(Gfx* cmd);

//////////////////////////////////

/*static unsigned long get_time(void) {
    return 0;
}*/

static void gfx_flush(void) {
    if (buf_vbo_len > 0) {
        gfx_rapi->draw_triangles(buf_vbo, buf_vbo_len, buf_vbo_num_tris);
        buf_vbo_len = 0;
        buf_vbo_num_tris = 0;
    }
}

static void combine_mode_update_hash(struct CombineMode* cm) {
    uint64_t hash = 5381;

    cm->hash = 0;

    hash = (hash << 5) + hash + ((u64)cm->rgb1 << 32);
    if (cm->use_alpha) {
        hash = (hash << 5) + hash + ((u64)cm->alpha1);
    }

    if (cm->use_2cycle) {
        hash = (hash << 5) + hash + ((u64)cm->rgb2 << 32);
        if (cm->use_alpha) {
            hash = (hash << 5) + hash + ((u64)cm->alpha2);
        }
    }

    hash = (hash << 5) + hash + cm->flags;

    cm->hash = hash;
}

static void color_combiner_update_hash(struct ColorCombiner* cc) {
    uint64_t hash = cc->cm.hash;

    for (int i = 0; i < 8; i++) {
        hash = (hash << 5) + hash + cc->shader_input_mapping_as_u64[i];
        hash = (hash << 5) + hash + cc->shader_commands_as_u64[i];
    }

    cc->hash = hash;
}

static struct ShaderProgram *gfx_lookup_or_create_shader_program(struct ColorCombiner* cc) {
    struct ShaderProgram *prg = gfx_rapi->lookup_shader(cc);
    if (prg == NULL) {
        gfx_rapi->unload_shader(rendering_state.shader_program);
        prg = gfx_rapi->create_and_load_new_shader(cc);
        rendering_state.shader_program = prg;
    }
    return prg;
}

static void gfx_generate_cc(struct ColorCombiner *cc) {
    u8 next_input_number = 0;
    u8 input_number[CC_ENUM_MAX] = { 0 };

    for  (int i = 0; i < SHADER_CMD_LENGTH; i++) {
        u8 cm_cmd = cc->cm.all_values[i];
        u8 shader_cmd = 0;
        switch (cm_cmd) {
            case CC_0:
                shader_cmd = SHADER_0;
                break;
            case CC_1:
                shader_cmd = SHADER_1;
                break;
            case CC_TEXEL0:
                shader_cmd = SHADER_TEXEL0;
                break;
            case CC_TEXEL1:
                shader_cmd = SHADER_TEXEL1;
                break;
            case CC_TEXEL0A:
                shader_cmd = SHADER_TEXEL0A;
                break;
            case CC_TEXEL1A:
                shader_cmd = SHADER_TEXEL1A;
                break;
            case CC_COMBINED:
                shader_cmd = cc->cm.use_2cycle ? SHADER_COMBINED : SHADER_0;
                break;
            case CC_COMBINEDA:
                shader_cmd = cc->cm.use_2cycle ? SHADER_COMBINEDA : SHADER_0;
                break;
            case CC_NOISE:
                shader_cmd = SHADER_NOISE;
                break;
            case CC_PRIM:
            case CC_PRIMA:
            case CC_SHADE:
            case CC_SHADEA:
            case CC_ENV:
            case CC_ENVA:
            case CC_LOD:
                if (input_number[cm_cmd] == 0) {
                    cc->shader_input_mapping[next_input_number] = cm_cmd;
                    input_number[cm_cmd] = SHADER_INPUT_1 + next_input_number;
                    next_input_number++;
                }
                shader_cmd = input_number[cm_cmd];
                break;
            default:
                shader_cmd = SHADER_0;
                break;
        }
        cc->shader_commands[i] = shader_cmd;
    }

    color_combiner_update_hash(cc);
    cc->prg = gfx_lookup_or_create_shader_program(cc);
    gfx_cc_print(cc);
}

static struct ColorCombiner *gfx_lookup_or_create_color_combiner(struct CombineMode* cm) {
    combine_mode_update_hash(cm);

    static struct ColorCombiner *prev_combiner;
    if (prev_combiner != NULL && prev_combiner->cm.hash == cm->hash) {
        return prev_combiner;
    }

    for (size_t i = 0; i < color_combiner_pool_size; i++) {
        if (color_combiner_pool[i].cm.hash == cm->hash) {
            return prev_combiner = &color_combiner_pool[i];
        }
    }

    gfx_flush();

    struct ColorCombiner *comb = &color_combiner_pool[color_combiner_pool_index];
    color_combiner_pool_index = (color_combiner_pool_index + 1) % CC_MAX_SHADERS;
    if (color_combiner_pool_size < CC_MAX_SHADERS) { color_combiner_pool_size++; }

    memcpy(&comb->cm, cm, sizeof(struct CombineMode));
    gfx_generate_cc(comb);

    return prev_combiner = comb;
}

void gfx_texture_cache_clear(void) {
    memset(&gfx_texture_cache, 0, sizeof(gfx_texture_cache));
}

static bool gfx_texture_cache_lookup(int tile, struct TextureHashmapNode **n, const uint8_t *orig_addr, uint32_t fmt, uint32_t siz) {
    size_t hash = (uintptr_t)orig_addr;
#define CMPADDR(x, y) x == y

    hash = (hash >> HASH_SHIFT) & HASH_MASK;

    struct TextureHashmapNode **node = &gfx_texture_cache.hashmap[hash];
    while (node != NULL && *node != NULL && *node - gfx_texture_cache.pool < (int)gfx_texture_cache.pool_pos) {
        if (CMPADDR((*node)->texture_addr, orig_addr) && (*node)->fmt == fmt && (*node)->siz == siz) {
            gfx_rapi->select_texture(tile, (*node)->texture_id);
            *n = *node;
            return true;
        }
        node = &(*node)->next;
    }
    if (gfx_texture_cache.pool_pos >= sizeof(gfx_texture_cache.pool) / sizeof(struct TextureHashmapNode)) {
        // Pool is full. We just invalidate everything and start over.
        gfx_texture_cache.pool_pos = 0;
        node = &gfx_texture_cache.hashmap[hash];
        // puts("Clearing texture cache");
    }
    if (!node) { return false; }
    *node = &gfx_texture_cache.pool[gfx_texture_cache.pool_pos++];
    if ((*node)->texture_addr == NULL) {
        (*node)->texture_id = gfx_rapi->new_texture();
    }
    gfx_rapi->select_texture(tile, (*node)->texture_id);
    gfx_rapi->set_sampler_parameters(tile, false, 0, 0);
    (*node)->next = NULL;
    (*node)->texture_addr = orig_addr;
    (*node)->fmt = fmt;
    (*node)->siz = siz;
    (*node)->cms = 0;
    (*node)->cmt = 0;
    (*node)->linear_filter = false;
    *n = *node;
    return false;
    #undef CMPADDR
}

static void import_texture_rgba32(int tile) {
    tile = tile % RDP_TILES;
    if (!rdp.loaded_texture[tile].addr) { return; }
    uint32_t width = rdp.texture_tile.line_size_bytes / 2;
    uint32_t height = (rdp.loaded_texture[tile].size_bytes / 2) / rdp.texture_tile.line_size_bytes;
    gfx_rapi->upload_texture(rdp.loaded_texture[tile].addr, width, height);
}

static void import_texture_rgba16(int tile) {
    tile = tile % RDP_TILES;
    if (!rdp.loaded_texture[tile].addr) { return; }
    if (rdp.loaded_texture[tile].size_bytes * 2 > 8192) { return; }
    uint8_t rgba32_buf[8192];

    for (uint32_t i = 0; i < rdp.loaded_texture[tile].size_bytes / 2; i++) {
        uint16_t col16 = (rdp.loaded_texture[tile].addr[2 * i] << 8) | rdp.loaded_texture[tile].addr[2 * i + 1];
        uint8_t a = col16 & 1;
        uint8_t r = col16 >> 11;
        uint8_t g = (col16 >> 6) & 0x1f;
        uint8_t b = (col16 >> 1) & 0x1f;
        rgba32_buf[4*i + 0] = SCALE_5_8(r);
        rgba32_buf[4*i + 1] = SCALE_5_8(g);
        rgba32_buf[4*i + 2] = SCALE_5_8(b);
        rgba32_buf[4*i + 3] = a ? 255 : 0;
    }

    uint32_t width = rdp.texture_tile.line_size_bytes / 2;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture_ia4(int tile) {
    tile = tile % RDP_TILES;
    if (!rdp.loaded_texture[tile].addr) { return; }
    if (rdp.loaded_texture[tile].size_bytes * 8 > 32768) { return; }
    uint8_t rgba32_buf[32768];

    for (uint32_t i = 0; i < rdp.loaded_texture[tile].size_bytes * 2; i++) {
        uint8_t byte = rdp.loaded_texture[tile].addr[i / 2];
        uint8_t part = (byte >> (4 - (i % 2) * 4)) & 0xf;
        uint8_t intensity = part >> 1;
        uint8_t alpha = part & 1;
        uint8_t r = intensity;
        uint8_t g = intensity;
        uint8_t b = intensity;
        rgba32_buf[4*i + 0] = SCALE_3_8(r);
        rgba32_buf[4*i + 1] = SCALE_3_8(g);
        rgba32_buf[4*i + 2] = SCALE_3_8(b);
        rgba32_buf[4*i + 3] = alpha ? 255 : 0;
    }

    uint32_t width = rdp.texture_tile.line_size_bytes * 2;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture_ia8(int tile) {
    tile = tile % RDP_TILES;
    if (!rdp.loaded_texture[tile].addr) { return; }
    if (rdp.loaded_texture[tile].size_bytes * 4 > 16384) { return; }
    uint8_t rgba32_buf[16384];

    for (uint32_t i = 0; i < rdp.loaded_texture[tile].size_bytes; i++) {
        uint8_t intensity = rdp.loaded_texture[tile].addr[i] >> 4;
        uint8_t alpha = rdp.loaded_texture[tile].addr[i] & 0xf;
        uint8_t r = intensity;
        uint8_t g = intensity;
        uint8_t b = intensity;
        rgba32_buf[4*i + 0] = SCALE_4_8(r);
        rgba32_buf[4*i + 1] = SCALE_4_8(g);
        rgba32_buf[4*i + 2] = SCALE_4_8(b);
        rgba32_buf[4*i + 3] = SCALE_4_8(alpha);
    }

    uint32_t width = rdp.texture_tile.line_size_bytes;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture_ia16(int tile) {
    tile = tile % RDP_TILES;
    if (!rdp.loaded_texture[tile].addr) { return; }
    if (rdp.loaded_texture[tile].size_bytes * 2 > 8192) { return; }
    uint8_t rgba32_buf[8192];

    for (uint32_t i = 0; i < rdp.loaded_texture[tile].size_bytes / 2; i++) {
        uint8_t intensity = rdp.loaded_texture[tile].addr[2 * i];
        uint8_t alpha = rdp.loaded_texture[tile].addr[2 * i + 1];
        uint8_t r = intensity;
        uint8_t g = intensity;
        uint8_t b = intensity;
        rgba32_buf[4*i + 0] = r;
        rgba32_buf[4*i + 1] = g;
        rgba32_buf[4*i + 2] = b;
        rgba32_buf[4*i + 3] = alpha;
    }

    uint32_t width = rdp.texture_tile.line_size_bytes / 2;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture_i4(int tile) {
    tile = tile % RDP_TILES;
    if (!rdp.loaded_texture[tile].addr) { return; }
    if (rdp.loaded_texture[tile].size_bytes * 8 > 32768) { return; }
    uint8_t rgba32_buf[32768];

    for (uint32_t i = 0; i < rdp.loaded_texture[tile].size_bytes * 2; i++) {
        uint8_t byte = rdp.loaded_texture[tile].addr[i / 2];
        uint8_t intensity = (byte >> (4 - (i % 2) * 4)) & 0xf;
        rgba32_buf[4*i + 0] = SCALE_4_8(intensity);
        rgba32_buf[4*i + 1] = SCALE_4_8(intensity);
        rgba32_buf[4*i + 2] = SCALE_4_8(intensity);
        rgba32_buf[4*i + 3] = 255;
    }

    uint32_t width = rdp.texture_tile.line_size_bytes * 2;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture_i8(int tile) {
    tile = tile % RDP_TILES;
    if (!rdp.loaded_texture[tile].addr) { return; }
    if (rdp.loaded_texture[tile].size_bytes * 4 > 16384) { return; }
    uint8_t rgba32_buf[16384];

    for (uint32_t i = 0; i < rdp.loaded_texture[tile].size_bytes; i++) {
        uint8_t intensity = rdp.loaded_texture[tile].addr[i];
        rgba32_buf[4*i + 0] = intensity;
        rgba32_buf[4*i + 1] = intensity;
        rgba32_buf[4*i + 2] = intensity;
        rgba32_buf[4*i + 3] = 255;
    }

    uint32_t width = rdp.texture_tile.line_size_bytes;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture_ci4(int tile) {
    tile = tile % RDP_TILES;
    if (!rdp.loaded_texture[tile].addr) { return; }
    if (rdp.loaded_texture[tile].size_bytes * 8 > 32768) { return; }
    uint8_t rgba32_buf[32768];

    for (uint32_t i = 0; i < rdp.loaded_texture[tile].size_bytes * 2; i++) {
        uint8_t byte = rdp.loaded_texture[tile].addr[i / 2];
        uint8_t idx = (byte >> (4 - (i % 2) * 4)) & 0xf;
        uint16_t col16 = (rdp.palette[idx * 2] << 8) | rdp.palette[idx * 2 + 1]; // Big endian load
        uint8_t a = col16 & 1;
        uint8_t r = col16 >> 11;
        uint8_t g = (col16 >> 6) & 0x1f;
        uint8_t b = (col16 >> 1) & 0x1f;
        rgba32_buf[4*i + 0] = SCALE_5_8(r);
        rgba32_buf[4*i + 1] = SCALE_5_8(g);
        rgba32_buf[4*i + 2] = SCALE_5_8(b);
        rgba32_buf[4*i + 3] = a ? 255 : 0;
    }

    uint32_t width = rdp.texture_tile.line_size_bytes * 2;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture_ci8(int tile) {
    tile = tile % RDP_TILES;
    if (!rdp.loaded_texture[tile].addr) { return; }
    if (rdp.loaded_texture[tile].size_bytes * 4 > 16384) { return; }
    uint8_t rgba32_buf[16384];

    for (uint32_t i = 0; i < rdp.loaded_texture[tile].size_bytes; i++) {
        uint8_t idx = rdp.loaded_texture[tile].addr[i];
        uint16_t col16 = (rdp.palette[idx * 2] << 8) | rdp.palette[idx * 2 + 1]; // Big endian load
        uint8_t a = col16 & 1;
        uint8_t r = col16 >> 11;
        uint8_t g = (col16 >> 6) & 0x1f;
        uint8_t b = (col16 >> 1) & 0x1f;
        rgba32_buf[4*i + 0] = SCALE_5_8(r);
        rgba32_buf[4*i + 1] = SCALE_5_8(g);
        rgba32_buf[4*i + 2] = SCALE_5_8(b);
        rgba32_buf[4*i + 3] = a ? 255 : 0;
    }

    uint32_t width = rdp.texture_tile.line_size_bytes;
    uint32_t height = rdp.loaded_texture[tile].size_bytes / rdp.texture_tile.line_size_bytes;

    gfx_rapi->upload_texture(rgba32_buf, width, height);
}

static void import_texture(int tile) {
    tile = tile % RDP_TILES;
    extern s32 dynos_tex_import(void **output, void *ptr, s32 tile, void *grapi, void **hashmap, void *pool, s32 *poolpos, s32 poolsize);
    if (dynos_tex_import((void **) &rendering_state.textures[tile], (void *) rdp.loaded_texture[tile].addr, tile, gfx_rapi, (void **) gfx_texture_cache.hashmap, (void *) gfx_texture_cache.pool, (int *) &gfx_texture_cache.pool_pos, MAX_CACHED_TEXTURES)) { return; }
    uint8_t fmt = rdp.texture_tile.fmt;
    uint8_t siz = rdp.texture_tile.siz;

    if (!rdp.loaded_texture[tile].addr) {
#ifdef DEVELOPMENT
/*
        fprintf(stderr, "NULL texture: tile %d, format %d/%d, size %d\n",
                tile, (int)fmt, (int)siz, (int)rdp.loaded_texture[tile].size_bytes);
*/
#endif
        return;
    }

    if (gfx_texture_cache_lookup(tile, &rendering_state.textures[tile], rdp.loaded_texture[tile].addr, fmt, siz)) {
        return;
    }

    // the texture data is actual texture data
    //int t0 = get_time();
    if (fmt == G_IM_FMT_RGBA) {
        if (siz == G_IM_SIZ_32b) {
            import_texture_rgba32(tile);
        }
        else if (siz == G_IM_SIZ_16b) {
            import_texture_rgba16(tile);
        } else {
            sys_fatal("unsupported RGBA texture size: %u", siz);
        }
    } else if (fmt == G_IM_FMT_IA) {
        if (siz == G_IM_SIZ_4b) {
            import_texture_ia4(tile);
        } else if (siz == G_IM_SIZ_8b) {
            import_texture_ia8(tile);
        } else if (siz == G_IM_SIZ_16b) {
            import_texture_ia16(tile);
        } else {
            sys_fatal("unsupported IA texture size: %u", siz);
        }
    } else if (fmt == G_IM_FMT_CI) {
        if (siz == G_IM_SIZ_4b) {
            import_texture_ci4(tile);
        } else if (siz == G_IM_SIZ_8b) {
            import_texture_ci8(tile);
        } else {
            sys_fatal("unsupported CI texture size: %u", siz);
        }
    } else if (fmt == G_IM_FMT_I) {
        if (siz == G_IM_SIZ_4b) {
            import_texture_i4(tile);
        } else if (siz == G_IM_SIZ_8b) {
            import_texture_i8(tile);
        } else {
            sys_fatal("unsupported I texture size: %u", siz);
        }
    } else {
        sys_fatal("unsupported texture format: %u", fmt);
    }
    //int t1 = get_time();
    //printf("Time diff: %d\n", t1 - t0);
}

static void OPTIMIZE_O3 gfx_transposed_matrix_mul(Vec3f res, const Vec3f a, const Mat4 b) {
    res[0] = a[0] * b[0][0] + a[1] * b[0][1] + a[2] * b[0][2];
    res[1] = a[0] * b[1][0] + a[1] * b[1][1] + a[2] * b[1][2];
    res[2] = a[0] * b[2][0] + a[1] * b[2][1] + a[2] * b[2][2];
}

static void calculate_normal_dir(const Light_t *light, Vec3f coeffs, bool applyLightingDir) {
    float light_dir[3] = {
        light->dir[0] / 127.0f,
        light->dir[1] / 127.0f,
        light->dir[2] / 127.0f
    };

    if (applyLightingDir) {
        light_dir[0] += gLightingDir[0];
        light_dir[1] += gLightingDir[1];
        light_dir[2] += gLightingDir[2];
    }

    gfx_transposed_matrix_mul(coeffs, light_dir, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1]);
    vec3f_normalize(coeffs);
}

static void OPTIMIZE_O3 gfx_sp_matrix(uint8_t parameters, const int32_t *addr) {
    Mat4 matrix;
#if 0
    // Original code when fixed point matrices were used
    for (int32_t i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            int32_t int_part = addr[i * 2 + j / 2];
            uint32_t frac_part = addr[8 + i * 2 + j / 2];
            matrix[i][j] = (int32_t)((int_part & 0xffff0000) | (frac_part >> 16)) / 65536.0f;
            matrix[i][j + 1] = (int32_t)((int_part << 16) | (frac_part & 0xffff)) / 65536.0f;
        }
    }
#else
    memcpy(matrix, addr, sizeof(matrix));
#endif

    if (parameters & G_MTX_PROJECTION) {
        if (parameters & G_MTX_LOAD) {
            mtxf_copy(rsp.P_matrix, matrix);
        } else {
            mtxf_mul(rsp.P_matrix, matrix, rsp.P_matrix);
        }
    } else { // G_MTX_MODELVIEW
        if ((parameters & G_MTX_PUSH) && rsp.modelview_matrix_stack_size < MAX_MATRIX_STACK_SIZE) {
            ++rsp.modelview_matrix_stack_size;
            mtxf_copy(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 2]);
        }
        if (parameters & G_MTX_LOAD) {
            mtxf_copy(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], matrix);
        } else {
            mtxf_mul(rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], matrix, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1]);
        }
        rsp.lights_changed = 1;
    }
    mtxf_mul(rsp.MP_matrix, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], rsp.P_matrix);
}

static void gfx_sp_pop_matrix(uint32_t count) {
    while (count--) {
        if (rsp.modelview_matrix_stack_size > 0) {
            --rsp.modelview_matrix_stack_size;
            if (rsp.modelview_matrix_stack_size > 0) {
                mtxf_mul(rsp.MP_matrix, rsp.modelview_matrix_stack[rsp.modelview_matrix_stack_size - 1], rsp.P_matrix);
            }
        }
    }
}

static float gfx_adjust_x_for_aspect_ratio(float x) {
    return x * gfx_current_dimensions.x_adjust_ratio;
}

static void OPTIMIZE_O3 gfx_sp_vertex(size_t n_vertices, size_t dest_index, const Vtx *vertices, bool luaVertexColor) {
    if (!vertices) { return; }

    Vec3f globalLightCached[2];
    Vec3f vertexColorCached;
    if (rsp.geometry_mode & G_LIGHTING) {
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 3; j++)
                globalLightCached[i][j] = gLightingColor[i][j] / 255.0f;
        }
    }

    if (luaVertexColor) {
        if ((rsp.geometry_mode & G_PACKED_NORMALS_EXT) || (!(rsp.geometry_mode & G_LIGHTING))) {
            for (int i = 0; i < 3; i ++) {
                vertexColorCached[i] = gVertexColor[i] / 255.0f;
            }
        }
    }

#ifdef __SSE__
    __m128 mat0 = _mm_load_ps(rsp.MP_matrix[0]);
    __m128 mat1 = _mm_load_ps(rsp.MP_matrix[1]);
    __m128 mat2 = _mm_load_ps(rsp.MP_matrix[2]);
    __m128 mat3 = _mm_load_ps(rsp.MP_matrix[3]);
#endif

    for (size_t i = 0; i < n_vertices; i++, dest_index++) {
        const Vtx_t *v = &vertices[i].v;
        const Vtx_tn *vn = &vertices[i].n;
        struct GfxVertex *d = &rsp.loaded_vertices[dest_index];

#ifdef __SSE__
        __m128 ob0 = _mm_set1_ps(v->ob[0]);
        __m128 ob1 = _mm_set1_ps(v->ob[1]);
        __m128 ob2 = _mm_set1_ps(v->ob[2]);

        __m128 pos = _mm_add_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(ob0, mat0), _mm_mul_ps(ob1, mat1)), _mm_mul_ps(ob2, mat2)), mat3);
        float x = pos[0];
        float y = pos[1];
        float z = pos[2];
        float w = pos[3];
#else
        float x = v->ob[0] * rsp.MP_matrix[0][0] + v->ob[1] * rsp.MP_matrix[1][0] + v->ob[2] * rsp.MP_matrix[2][0] + rsp.MP_matrix[3][0];
        float y = v->ob[0] * rsp.MP_matrix[0][1] + v->ob[1] * rsp.MP_matrix[1][1] + v->ob[2] * rsp.MP_matrix[2][1] + rsp.MP_matrix[3][1];
        float z = v->ob[0] * rsp.MP_matrix[0][2] + v->ob[1] * rsp.MP_matrix[1][2] + v->ob[2] * rsp.MP_matrix[2][2] + rsp.MP_matrix[3][2];
        float w = v->ob[0] * rsp.MP_matrix[0][3] + v->ob[1] * rsp.MP_matrix[1][3] + v->ob[2] * rsp.MP_matrix[2][3] + rsp.MP_matrix[3][3];
#endif

        x = gfx_adjust_x_for_aspect_ratio(x);

        short U = v->tc[0] * rsp.texture_scaling_factor.s >> 16;
        short V = v->tc[1] * rsp.texture_scaling_factor.t >> 16;

        if (rsp.geometry_mode & G_LIGHTING) {
            if (rsp.lights_changed) {
                bool applyLightingDir = !(rsp.geometry_mode & G_TEXTURE_GEN);
                for (int32_t i = 0; i < rsp.current_num_lights - 1; i++) {
                    calculate_normal_dir(&rsp.current_lights[i], rsp.current_lights_coeffs[i], applyLightingDir);
                }
                static const Light_t lookat_x = {{0, 0, 0}, 0, {0, 0, 0}, 0, {0, 127, 0}, 0};
                static const Light_t lookat_y = {{0, 0, 0}, 0, {0, 0, 0}, 0, {127, 0, 0}, 0};
                calculate_normal_dir(&lookat_x, rsp.current_lookat_coeffs[0], applyLightingDir);
                calculate_normal_dir(&lookat_y, rsp.current_lookat_coeffs[1], applyLightingDir);
                rsp.lights_changed = false;
            }

            float r = rsp.current_lights[rsp.current_num_lights - 1].col[0] * globalLightCached[1][0];
            float g = rsp.current_lights[rsp.current_num_lights - 1].col[1] * globalLightCached[1][1];
            float b = rsp.current_lights[rsp.current_num_lights - 1].col[2] * globalLightCached[1][2];

            signed char nx = vn->n[0];
            signed char ny = vn->n[1];
            signed char nz = vn->n[2];

            if (rsp.geometry_mode & G_PACKED_NORMALS_EXT) {
                unsigned short packedNormal = vn->flag;
                int xo = packedNormal >> 8;
                int yo = packedNormal & 0xFF;

                nx = xo & 0x7F;
                ny = yo & 0x7F;
                nz = (nx + ny) ^ 0x7F;

                if (nz & 0x80) {
                    nx ^= 0x7F;
                    ny ^= 0x7F;
                }

                nx = (xo & 0x80) ? -nx : nx;
                ny = (yo & 0x80) ? -ny : ny;

                SUPPORT_CHECK(absi(nx) + absi(ny) + absi(nz) == 127);
            }

            for (int32_t i = 0; i < rsp.current_num_lights - 1; i++) {
                float intensity = 0;

                intensity += nx * rsp.current_lights_coeffs[i][0];
                intensity += ny * rsp.current_lights_coeffs[i][1];
                intensity += nz * rsp.current_lights_coeffs[i][2];

                intensity /= 127.0f;
                if (intensity > 0.0f) {
                    r += intensity * rsp.current_lights[i].col[0] * globalLightCached[0][0];
                    g += intensity * rsp.current_lights[i].col[1] * globalLightCached[0][1];
                    b += intensity * rsp.current_lights[i].col[2] * globalLightCached[0][2];
                }
            }

            d->color.r = r > 255.0f ? 255 : (uint8_t)r;
            d->color.g = g > 255.0f ? 255 : (uint8_t)g;
            d->color.b = b > 255.0f ? 255 : (uint8_t)b;

            if (rsp.geometry_mode & G_PACKED_NORMALS_EXT) {
                float vtxR = (v->cn[0] / 255.0f);
                float vtxG = (v->cn[1] / 255.0f);
                float vtxB = (v->cn[2] / 255.0f);
                if (luaVertexColor) {
                    d->color.r *= vtxR * vertexColorCached[0];
                    d->color.g *= vtxG * vertexColorCached[1];
                    d->color.b *= vtxB * vertexColorCached[2];
                } else {
                    d->color.r *= vtxR;
                    d->color.g *= vtxG;
                    d->color.b *= vtxB;
                }
            }

            if (rsp.geometry_mode & G_TEXTURE_GEN) {
                float dotx = 0, doty = 0;
                dotx += nx * rsp.current_lookat_coeffs[0][0];
                dotx += ny * rsp.current_lookat_coeffs[0][1];
                dotx += nz * rsp.current_lookat_coeffs[0][2];
                doty += nx * rsp.current_lookat_coeffs[1][0];
                doty += ny * rsp.current_lookat_coeffs[1][1];
                doty += nz * rsp.current_lookat_coeffs[1][2];

                U = (int32_t)((dotx / 127.0f + 1.0f) / 4.0f * rsp.texture_scaling_factor.s);
                V = (int32_t)((doty / 127.0f + 1.0f) / 4.0f * rsp.texture_scaling_factor.t);
            }

            if (rsp.geometry_mode & G_LIGHTING_ENGINE_EXT) {
                Color color;
                CTX_BEGIN(CTX_LIGHTING);
                le_calculate_lighting_color(((Vtx_t*)v)->ob, color, 1.0f);
                CTX_END(CTX_LIGHTING);

                d->color.r *= color[0] / 255.0f;
                d->color.g *= color[1] / 255.0f;
                d->color.b *= color[2] / 255.0f;
            }
        } else if (rsp.geometry_mode & G_LIGHTING_ENGINE_EXT) {
            Color color;
            CTX_BEGIN(CTX_LIGHTING);
            le_calculate_vertex_lighting((Vtx_t*)v, color);
            CTX_END(CTX_LIGHTING);
            if (luaVertexColor) {
                d->color.r = color[0] * vertexColorCached[0];
                d->color.g = color[1] * vertexColorCached[1];
                d->color.b = color[2] * vertexColorCached[2];
            } else {
                d->color.r = color[0];
                d->color.g = color[1];
                d->color.b = color[2];
            }
        } else {
            if (!(rsp.geometry_mode & G_LIGHT_MAP_EXT) && luaVertexColor) {
                d->color.r = v->cn[0] * vertexColorCached[0];
                d->color.g = v->cn[1] * vertexColorCached[1];
                d->color.b = v->cn[2] * vertexColorCached[2];
            } else {
                d->color.r = v->cn[0];
                d->color.g = v->cn[1];
                d->color.b = v->cn[2];
            }
        }

        d->u = U;
        d->v = V;

        // trivial clip rejection
        d->clip_rej = 0;
        if (x < -w) d->clip_rej |= 1;
        if (x > w) d->clip_rej |= 2;
        if (y < -w) d->clip_rej |= 4;
        if (y > w) d->clip_rej |= 8;
        if (z < -w) d->clip_rej |= 16;
        if (z > w) d->clip_rej |= 32;

        d->x = x;
        d->y = y;
        d->z = z;
        d->w = w;

        if (rsp.geometry_mode & G_FOG) {
            if (fabsf(w) < 0.001f) {
                // To avoid division by zero
                w = 0.001f;
            }

            float winv = 1.0f / w;
            if (winv < 0.0f) {
                winv = 32767.0f;
            }

            z -= sDepthZSub;
            z *= sDepthZMult;
            z += sDepthZAdd;

            float fog_z = z * winv * rsp.fog_mul * gFogIntensity + rsp.fog_offset;

            if (fog_z < 0) fog_z = 0;
            if (fog_z > 255) fog_z = 255;
            d->fog_z = fog_z;
        }

        d->color.a = v->cn[3];
    }
}

static void OPTIMIZE_O3 gfx_sp_tri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx) {
    struct GfxVertex *v1 = &rsp.loaded_vertices[vtx1_idx];
    struct GfxVertex *v2 = &rsp.loaded_vertices[vtx2_idx];
    struct GfxVertex *v3 = &rsp.loaded_vertices[vtx3_idx];
    struct GfxVertex *v_arr[3] = {v1, v2, v3};

    if (v1->clip_rej & v2->clip_rej & v3->clip_rej) {
        // The whole triangle lies outside the visible area
        return;
    }

    if ((rsp.geometry_mode & G_CULL_BOTH) != 0) {
        float dx1 = v1->x / (v1->w) - v2->x / (v2->w);
        float dy1 = v1->y / (v1->w) - v2->y / (v2->w);
        float dx2 = v3->x / (v3->w) - v2->x / (v2->w);
        float dy2 = v3->y / (v3->w) - v2->y / (v2->w);
        float cross = dx1 * dy2 - dy1 * dx2;

        if ((v1->w < 0) ^ (v2->w < 0) ^ (v3->w < 0)) {
            // If one vertex lies behind the eye, negating cross will give the correct result.
            // If all vertices lie behind the eye, the triangle will be rejected anyway.
            cross = -cross;
        }

        switch (rsp.geometry_mode & G_CULL_BOTH) {
            case G_CULL_FRONT:
                if (cross <= 0) return;
                break;
            case G_CULL_BACK:
                if (cross >= 0) return;
                break;
            case G_CULL_BOTH:
                // Why is this even an option?
                // HACK: Instead of culling both sides and displaying nothing, cull nothing and display everything
                // this is needed because of the mirror room... some custom models will set/clear cull values resulting in cull both
                break;
        }
    }

    bool depth_test = (rsp.geometry_mode & G_ZBUFFER) == G_ZBUFFER;
    if (depth_test != rendering_state.depth_test) {
        gfx_flush();
        gfx_rapi->set_depth_test(depth_test);
        rendering_state.depth_test = depth_test;
    }

    bool z_upd = (rdp.other_mode_l & Z_UPD) == Z_UPD;
    if (z_upd != rendering_state.depth_mask) {
        gfx_flush();
        gfx_rapi->set_depth_mask(z_upd);
        rendering_state.depth_mask = z_upd;
    }

    bool zmode_decal = (rdp.other_mode_l & ZMODE_DEC) == ZMODE_DEC;
    if (zmode_decal != rendering_state.decal_mode) {
        gfx_flush();
        gfx_rapi->set_zmode_decal(zmode_decal);
        rendering_state.decal_mode = zmode_decal;
    }

    if (rdp.viewport_or_scissor_changed) {
        static uint32_t x_adjust_4by3_prev;
        if (memcmp(&rdp.viewport, &rendering_state.viewport, sizeof(rdp.viewport)) != 0
            || x_adjust_4by3_prev != gfx_current_dimensions.x_adjust_4by3) {
            gfx_flush();
            gfx_rapi->set_viewport(rdp.viewport.x + gfx_current_dimensions.x_adjust_4by3, rdp.viewport.y, rdp.viewport.width, rdp.viewport.height);
            rendering_state.viewport = rdp.viewport;
        }
        if (memcmp(&rdp.scissor, &rendering_state.scissor, sizeof(rdp.scissor)) != 0
            || x_adjust_4by3_prev != gfx_current_dimensions.x_adjust_4by3) {
            gfx_flush();
            gfx_rapi->set_scissor(rdp.scissor.x + gfx_current_dimensions.x_adjust_4by3, rdp.scissor.y, rdp.scissor.width, rdp.scissor.height);
            rendering_state.scissor = rdp.scissor;
        }
        rdp.viewport_or_scissor_changed = false;
        x_adjust_4by3_prev = gfx_current_dimensions.x_adjust_4by3;
    }

    struct CombineMode* cm = &rdp.combine_mode;

    cm->use_alpha    = (rdp.other_mode_l & (G_BL_A_MEM << 18))        == 0;
    cm->texture_edge = (rdp.other_mode_l & CVG_X_ALPHA)               == CVG_X_ALPHA;
    cm->use_dither   = (rdp.other_mode_l & G_AC_DITHER)               == G_AC_DITHER;
    cm->use_2cycle   = (rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_2CYCLE;
    cm->use_fog      = (rdp.other_mode_l >> 30)                       == G_BL_CLR_FOG;
    cm->light_map    = (rsp.geometry_mode & G_LIGHT_MAP_EXT)          == G_LIGHT_MAP_EXT;

    if (cm->texture_edge) {
        cm->use_alpha = true;
    }

    // hack: disable 2cycle if it uses a second texture that doesn't exist
    // this is because old rom hacks were ported assuming that 2cycle didn't exist
    // and were ported incorrectly
    if (!rdp.loaded_texture[1].addr && cm->use_2cycle && gfx_cm_uses_second_texture(cm)) {
        cm->use_2cycle = false;
    }

    struct ColorCombiner *comb = gfx_lookup_or_create_color_combiner(cm);
    cm = &comb->cm;

    struct ShaderProgram *prg = comb->prg;
    if (prg != rendering_state.shader_program) {
        gfx_flush();
        gfx_rapi->unload_shader(rendering_state.shader_program);
        gfx_rapi->load_shader(prg);
        rendering_state.shader_program = prg;
    }
    if (cm->use_alpha != rendering_state.alpha_blend) {
        gfx_flush();
        gfx_rapi->set_use_alpha(cm->use_alpha);
        rendering_state.alpha_blend = cm->use_alpha;
    }
    uint8_t num_inputs;
    bool used_textures[2];
    gfx_rapi->shader_get_info(prg, &num_inputs, used_textures);

    for (int32_t i = 0; i < 2; i++) {
        if (used_textures[i]) {
            if (rdp.textures_changed[i]) {
                gfx_flush();
                import_texture(i);
                rdp.textures_changed[i] = false;
            }
            bool linear_filter = configFiltering && ((rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT);
            struct TextureHashmapNode* tex = rendering_state.textures[i];
            if (tex) {
                if (linear_filter != tex->linear_filter || rdp.texture_tile.cms != tex->cms || rdp.texture_tile.cmt != rendering_state.textures[i]->cmt) {
                    gfx_flush();
                    gfx_rapi->set_sampler_parameters(i, linear_filter, rdp.texture_tile.cms, rdp.texture_tile.cmt);
                    tex->linear_filter = linear_filter;
                    tex->cms = rdp.texture_tile.cms;
                    tex->cmt = rdp.texture_tile.cmt;
                }
            }
        }
    }

    bool use_texture = used_textures[0] || used_textures[1];
    uint32_t tex_width = (rdp.texture_tile.lrs - rdp.texture_tile.uls + 4) / 4;
    uint32_t tex_height = (rdp.texture_tile.lrt - rdp.texture_tile.ult + 4) / 4;

    bool z_is_from_0_to_1 = gfx_rapi->z_is_from_0_to_1();

    for (int32_t i = 0; i < 3; i++) {
        float z = v_arr[i]->z, w = v_arr[i]->w;
        if (z_is_from_0_to_1) {
            z = (z + w) / 2.0f;
        }
        buf_vbo[buf_vbo_len++] = v_arr[i]->x;
        buf_vbo[buf_vbo_len++] = v_arr[i]->y;
        buf_vbo[buf_vbo_len++] = z;
        buf_vbo[buf_vbo_len++] = w;

        if (use_texture) {
            float u = (v_arr[i]->u - rdp.texture_tile.uls * 8) / 32.0f;
            float v = (v_arr[i]->v - rdp.texture_tile.ult * 8) / 32.0f;
            if ((rdp.other_mode_h & (3U << G_MDSFT_TEXTFILT)) != G_TF_POINT) {
                // Linear filter adds 0.5f to the coordinates (why?)
                u += 0.5f;
                v += 0.5f;
            }
            buf_vbo[buf_vbo_len++] = u / tex_width;
            buf_vbo[buf_vbo_len++] = v / tex_height;
        }

        if (cm->use_fog) {
            f32 r = gFogColor[0] / 255.0f;
            f32 g = gFogColor[1] / 255.0f;
            f32 b = gFogColor[2] / 255.0f;
            buf_vbo[buf_vbo_len++] = (rdp.fog_color.r / 255.0f) * r;
            buf_vbo[buf_vbo_len++] = (rdp.fog_color.g / 255.0f) * g;
            buf_vbo[buf_vbo_len++] = (rdp.fog_color.b / 255.0f) * b;
            buf_vbo[buf_vbo_len++] = v_arr[i]->fog_z / 255.0f; // fog factor (not alpha)
        }

        if (cm->light_map) {
            struct RGBA* col = &v_arr[i]->color;
            buf_vbo[buf_vbo_len++] = ( (((uint16_t)col->g) << 8) | ((uint16_t)col->r) ) / 65535.0f;
            buf_vbo[buf_vbo_len++] = 1.0f - (( (((uint16_t)col->a) << 8) | ((uint16_t)col->b) ) / 65535.0f);
        }

        for (int j = 0; j < num_inputs; j++) {
            struct RGBA *color = NULL;
            struct RGBA tmp = { 0 };
            for (int a = 0; a < (cm->use_alpha ? 2 : 1 ); a++) {
                u8 mapping = comb->shader_input_mapping[j];

                switch (mapping) {
                    case CC_PRIM:
                        color = &rdp.prim_color;
                        break;
                    case CC_SHADE:
                        color = &v_arr[i]->color;
                        break;
                    case CC_ENV:
                        color = &rdp.env_color;
                        break;
                    case CC_PRIMA:
                        memset(&tmp, rdp.prim_color.a, sizeof(tmp));
                        color = &tmp;
                        break;
                    case CC_SHADEA:
                        memset(&tmp, v_arr[i]->color.a, sizeof(tmp));
                        color = &tmp;
                        break;
                    case CC_ENVA:
                        memset(&tmp, rdp.env_color.a, sizeof(tmp));
                        color = &tmp;
                        break;
                    case CC_LOD:
                    {
                        float distance_frac = (v1->w - 3000.0f) / 3000.0f;
                        if (distance_frac < 0.0f) distance_frac = 0.0f;
                        if (distance_frac > 1.0f) distance_frac = 1.0f;
                        tmp.r = tmp.g = tmp.b = tmp.a = distance_frac * 255.0f;
                        color = &tmp;
                        break;
                    }
                    default:
                        memset(&tmp, 0, sizeof(tmp));
                        color = &tmp;
                        break;
                }
                if (a == 0) {
                    buf_vbo[buf_vbo_len++] = color->r / 255.0f;
                    buf_vbo[buf_vbo_len++] = color->g / 255.0f;
                    buf_vbo[buf_vbo_len++] = color->b / 255.0f;
                } else {
                    if (cm->use_fog && (color == &v_arr[i]->color || cm->light_map)) {
                        // Shade alpha is 100% for fog
                        buf_vbo[buf_vbo_len++] = 1.0f;
                    } else {
                        buf_vbo[buf_vbo_len++] = color->a / 255.0f;
                    }
                }
            }
        }
        /*struct RGBA *color = &v_arr[i]->color;
        buf_vbo[buf_vbo_len++] = color->r / 255.0f;
        buf_vbo[buf_vbo_len++] = color->g / 255.0f;
        buf_vbo[buf_vbo_len++] = color->b / 255.0f;
        buf_vbo[buf_vbo_len++] = color->a / 255.0f;*/
    }
    if (++buf_vbo_num_tris == MAX_BUFFERED) {
        gfx_flush();
    }
}

static void gfx_sp_geometry_mode(uint32_t clear, uint32_t set) {
    rsp.geometry_mode &= ~clear;
    rsp.geometry_mode |= set;
}

static void gfx_calc_and_set_viewport(const Vp_t *viewport) {
    // 2 bits fraction
    float width = 2.0f * viewport->vscale[0] / 4.0f;
    float height = 2.0f * viewport->vscale[1] / 4.0f;
    float x = (viewport->vtrans[0] / 4.0f) - width / 2.0f;
    float y = SCREEN_HEIGHT - ((viewport->vtrans[1] / 4.0f) + height / 2.0f);

    width *= RATIO_X;
    height *= RATIO_Y;
    x *= RATIO_X;
    y *= RATIO_Y;

    rdp.viewport.x = x;
    rdp.viewport.y = y;
    rdp.viewport.width = width;
    rdp.viewport.height = height;

    rdp.viewport_or_scissor_changed = true;
}

static void gfx_sp_movemem(uint8_t index, uint16_t offset, const void* data) {
    switch (index) {
        case G_MV_VIEWPORT:
            gfx_calc_and_set_viewport((const Vp_t *) data);
            break;
#if 0
        case G_MV_LOOKATY:
        case G_MV_LOOKATX:
            memcpy(rsp.current_lookat + (index - G_MV_LOOKATY) / 2, data, sizeof(Light_t));
            //rsp.lights_changed = 1;
            break;
#endif
#ifdef F3DEX_GBI_2
        case G_MV_LIGHT: {
            int lightidx = offset / 24 - 2;
            if (lightidx >= 0 && lightidx <= MAX_LIGHTS) { // skip lookat
                // NOTE: reads out of bounds if it is an ambient light
                memcpy(rsp.current_lights + lightidx, data, sizeof(Light_t));
            }
            break;
        }
#else
        case G_MV_L0:
        case G_MV_L1:
        case G_MV_L2:
        case G_MV_L3:
        case G_MV_L4:
        case G_MV_L5:
        case G_MV_L6:
            // NOTE: reads out of bounds if it is an ambient light
            memcpy(rsp.current_lights + (index - G_MV_L0) / 2, data, sizeof(Light_t));
            break;
#endif
    }
}

#ifdef F3DEX_GBI_2E
static void gfx_sp_copymem(uint8_t idx, uint16_t dstofs, uint16_t srcofs, UNUSED uint8_t words) {
    if (idx == G_MV_LIGHT) {
        const int srcidx = srcofs / 24 - 2;
        const int dstidx = dstofs / 24 - 2;
        if (srcidx <= MAX_LIGHTS && dstidx <= MAX_LIGHTS) {
            memcpy(rsp.current_lights + dstidx, rsp.current_lights + srcidx, sizeof(Light_t));
        }
    }
}
#endif

static void gfx_sp_moveword(uint8_t index, UNUSED uint16_t offset, uint32_t data) {
    switch (index) {
        case G_MW_NUMLIGHT:
#ifdef F3DEX_GBI_2
            rsp.current_num_lights = data / 24 + 1; // add ambient light
#else
            // Ambient light is included
            // The 31th bit is a flag that lights should be recalculated
            rsp.current_num_lights = (data - 0x80000000U) / 32;
#endif
            rsp.lights_changed = 1;
            break;
        case G_MW_FOG:
            rsp.fog_mul = (int16_t)(data >> 16);
            rsp.fog_offset = (int16_t)data;

            // Alter depth buffer to deal with new near plane
            sDepthZAdd = (gProjectionMaxNearValue - gProjectionVanillaNearValue) + gProjectionMaxNearValue;
            sDepthZMult = (gProjectionVanillaFarValue - gProjectionMaxNearValue) / (gProjectionVanillaFarValue - gProjectionVanillaNearValue);
            sDepthZSub = gProjectionVanillaNearValue;

            break;
    }
}

static void gfx_sp_texture(uint16_t sc, uint16_t tc, UNUSED uint8_t level, UNUSED uint8_t tile, UNUSED uint8_t on) {
    rsp.texture_scaling_factor.s = sc;
    rsp.texture_scaling_factor.t = tc;
}

static void gfx_dp_set_scissor(UNUSED uint32_t mode, uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry) {
    float x = ulx / 4.0f * RATIO_X;
    float y = (SCREEN_HEIGHT - lry / 4.0f) * RATIO_Y;
    float width = (lrx - ulx) / 4.0f * RATIO_X;
    float height = (lry - uly) / 4.0f * RATIO_Y;

    rdp.scissor.x = x;
    rdp.scissor.y = y;
    rdp.scissor.width = width;
    rdp.scissor.height = height;

    rdp.viewport_or_scissor_changed = true;
}

static void gfx_dp_set_texture_image(UNUSED uint32_t format, uint32_t size, UNUSED uint32_t width, const void* addr) {
    rdp.texture_to_load.addr = addr;
    rdp.texture_to_load.siz = size;
}

static void gfx_dp_set_tile(uint8_t fmt, uint32_t siz, uint32_t line, uint32_t tmem, uint8_t tile, uint32_t palette, uint32_t cmt, UNUSED uint32_t maskt, UNUSED uint32_t shiftt, uint32_t cms, UNUSED uint32_t masks, UNUSED uint32_t shifts) {
    if (tile == G_TX_RENDERTILE) {
        SUPPORT_CHECK(palette == 0); // palette should set upper 4 bits of color index in 4b mode
        rdp.texture_tile.fmt = fmt;
        rdp.texture_tile.siz = siz;
        rdp.texture_tile.cms = cms;
        rdp.texture_tile.cmt = cmt;
        rdp.texture_tile.line_size_bytes = line * 8;
        if (!sOnlyTextureChangeOnAddrChange) {
            // I don't know if we ever need to set these...
            rdp.textures_changed[0] = true;
            rdp.textures_changed[1] = true;
        }
    } else if (tile == G_TX_LOADTILE) {
        rdp.texture_to_load.tile_number = tmem / 256;
    } else if (tile == G_TX_LOADTILE_6_UNKNOWN) {
        // this is a hack, because it seems like we can only load two tiles at once currently
        rdp.texture_to_load.tile_number = 1;
    }
}

static void gfx_dp_set_tile_size(uint8_t tile, uint16_t uls, uint16_t ult, uint16_t lrs, uint16_t lrt) {
    if (tile == G_TX_RENDERTILE) {
        rdp.texture_tile.uls = uls;
        rdp.texture_tile.ult = ult;
        rdp.texture_tile.lrs = lrs;
        rdp.texture_tile.lrt = lrt;
        if (!sOnlyTextureChangeOnAddrChange) {
            // I don't know if we ever need to set these...
            rdp.textures_changed[0] = true;
            rdp.textures_changed[1] = true;
        }
    }
}

static void gfx_dp_load_tlut(UNUSED uint8_t tile, UNUSED uint32_t high_index) {
    SUPPORT_CHECK(rdp.texture_to_load.siz == G_IM_SIZ_16b);
    rdp.palette = rdp.texture_to_load.addr;
}

static void gfx_dp_load_block(UNUSED uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, UNUSED uint32_t dxt) {
    //if (tile == 1) return;
    SUPPORT_CHECK(uls == 0);
    SUPPORT_CHECK(ult == 0);

    // The lrs field rather seems to be number of pixels to load
    uint32_t word_size_shift = 0;
    switch (rdp.texture_to_load.siz) {
        case G_IM_SIZ_4b:
            word_size_shift = 0; // Or -1? It's unused in SM64 anyway.
            break;
        case G_IM_SIZ_8b:
            word_size_shift = 0;
            break;
        case G_IM_SIZ_16b:
            word_size_shift = 1;
            break;
        case G_IM_SIZ_32b:
            word_size_shift = 2;
            break;
    }
    uint32_t size_bytes = (lrs + 1) << word_size_shift;
    gfx_update_loaded_texture(rdp.texture_to_load.tile_number, size_bytes, rdp.texture_to_load.addr);
}

static void gfx_dp_load_tile(UNUSED uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt) {
    SUPPORT_CHECK(uls == 0);
    SUPPORT_CHECK(ult == 0);

    uint32_t word_size_shift = 0;
    switch (rdp.texture_to_load.siz) {
        case G_IM_SIZ_4b:
            word_size_shift = 0; // Or -1? It's unused in SM64 anyway.
            break;
        case G_IM_SIZ_8b:
            word_size_shift = 0;
            break;
        case G_IM_SIZ_16b:
            word_size_shift = 1;
            break;
        case G_IM_SIZ_32b:
            word_size_shift = 2;
            break;
    }

    uint32_t size_bytes = (((lrs >> G_TEXTURE_IMAGE_FRAC) + 1) * ((lrt >> G_TEXTURE_IMAGE_FRAC) + 1)) << word_size_shift;
    gfx_update_loaded_texture(rdp.texture_to_load.tile_number, size_bytes, rdp.texture_to_load.addr);
    rdp.texture_tile.uls = uls;
    rdp.texture_tile.ult = ult;
    rdp.texture_tile.lrs = lrs;
    rdp.texture_tile.lrt = lrt;
}

static void gfx_dp_set_combine_mode(uint32_t rgb1, uint32_t alpha1, uint32_t rgb2, uint32_t alpha2) {
    //printf(">>> combine: %08x %08x %08x %08x\n", rgb1, alpha1, rgb2, alpha2);
    memset(&rdp.combine_mode, 0, sizeof(struct CombineMode));

    rdp.combine_mode.rgb1 = rgb1;
    rdp.combine_mode.alpha1 = alpha1;

    rdp.combine_mode.rgb2 = rgb2;
    rdp.combine_mode.alpha2 = alpha2;

    rdp.combine_mode.flags = 0;
}

static void gfx_dp_set_env_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rdp.env_color.r = r;
    rdp.env_color.g = g;
    rdp.env_color.b = b;
    rdp.env_color.a = a;
}

static void gfx_dp_set_prim_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rdp.prim_color.r = r;
    rdp.prim_color.g = g;
    rdp.prim_color.b = b;
    rdp.prim_color.a = a;
}

static void gfx_dp_set_fog_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    rdp.fog_color.r = r;
    rdp.fog_color.g = g;
    rdp.fog_color.b = b;
    rdp.fog_color.a = a;
}

static void gfx_dp_set_fill_color(uint32_t packed_color) {
    uint16_t col16 = (uint16_t)packed_color;
    uint32_t r = col16 >> 11;
    uint32_t g = (col16 >> 6) & 0x1f;
    uint32_t b = (col16 >> 1) & 0x1f;
    uint32_t a = col16 & 1;
    rdp.fill_color.r = SCALE_5_8(r);
    rdp.fill_color.g = SCALE_5_8(g);
    rdp.fill_color.b = SCALE_5_8(b);
    rdp.fill_color.a = a * 255;
}

static void gfx_draw_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    uint32_t saved_other_mode_h = rdp.other_mode_h;
    uint32_t cycle_type = (rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE));

    if (cycle_type == G_CYC_COPY) {
        rdp.other_mode_h = (rdp.other_mode_h & ~(3U << G_MDSFT_TEXTFILT)) | G_TF_POINT;
    }

    // U10.2 coordinates
    float ulxf = ulx;
    float ulyf = uly;
    float lrxf = lrx;
    float lryf = lry;

    ulxf = ulxf / (4.0f * HALF_SCREEN_WIDTH) - 1.0f;
    ulyf = -(ulyf / (4.0f * HALF_SCREEN_HEIGHT)) + 1.0f;
    lrxf = lrxf / (4.0f * HALF_SCREEN_WIDTH) - 1.0f;
    lryf = -(lryf / (4.0f * HALF_SCREEN_HEIGHT)) + 1.0f;

    ulxf = gfx_adjust_x_for_aspect_ratio(ulxf);
    lrxf = gfx_adjust_x_for_aspect_ratio(lrxf);

    struct GfxVertex* ul = &rsp.loaded_vertices[MAX_VERTICES + 0];
    struct GfxVertex* ll = &rsp.loaded_vertices[MAX_VERTICES + 1];
    struct GfxVertex* lr = &rsp.loaded_vertices[MAX_VERTICES + 2];
    struct GfxVertex* ur = &rsp.loaded_vertices[MAX_VERTICES + 3];

    ul->x = ulxf;
    ul->y = ulyf;
    ul->z = -1.0f;
    ul->w = 1.0f;

    ll->x = ulxf;
    ll->y = lryf;
    ll->z = -1.0f;
    ll->w = 1.0f;

    lr->x = lrxf;
    lr->y = lryf;
    lr->z = -1.0f;
    lr->w = 1.0f;

    ur->x = lrxf;
    ur->y = ulyf;
    ur->z = -1.0f;
    ur->w = 1.0f;

    // The coordinates for texture rectangle shall bypass the viewport setting
    struct Box default_viewport = {0, 0, gfx_current_dimensions.width, gfx_current_dimensions.height};
    struct Box viewport_saved = rdp.viewport;
    uint32_t geometry_mode_saved = rsp.geometry_mode;

    rdp.viewport = default_viewport;
    rdp.viewport_or_scissor_changed = true;
    rsp.geometry_mode = 0;

    gfx_sp_tri1(MAX_VERTICES + 0, MAX_VERTICES + 1, MAX_VERTICES + 3);
    gfx_sp_tri1(MAX_VERTICES + 1, MAX_VERTICES + 2, MAX_VERTICES + 3);

    rsp.geometry_mode = geometry_mode_saved;
    rdp.viewport = viewport_saved;
    rdp.viewport_or_scissor_changed = true;

    if (cycle_type == G_CYC_COPY) {
        rdp.other_mode_h = saved_other_mode_h;
    }
}

static void gfx_dp_texture_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry, UNUSED uint8_t tile, int16_t uls, int16_t ult, int16_t dsdx, int16_t dtdy, bool flip) {
    struct CombineMode saved_combine_mode = rdp.combine_mode;
    if ((rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE)) == G_CYC_COPY) {
        // Per RDP Command Summary Set Tile's shift s and this dsdx should be set to 4 texels
        // Divide by 4 to get 1 instead
        dsdx >>= 2;

        // Color combiner is turned off in copy mode
        gfx_dp_set_combine_mode(
            color_comb_rgb  (G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_TEXEL0, 0),
            color_comb_alpha(G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_ACMUX_TEXEL0, 0),
            color_comb_rgb  (G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_TEXEL0, 1),
            color_comb_alpha(G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_ACMUX_TEXEL0, 1));

        // Per documentation one extra pixel is added in this modes to each edge
        lrx += 1 << 2;
        lry += 1 << 2;
    }

    // uls and ult are S10.5
    // dsdx and dtdy are S5.10
    // lrx, lry, ulx, uly are U10.2
    // lrs, lrt are S10.5
    if (flip) {
        dsdx = -dsdx;
        dtdy = -dtdy;
    }
    int16_t width = !flip ? lrx - ulx : lry - uly;
    int16_t height = !flip ? lry - uly : lrx - ulx;
    float lrs = ((uls << 7) + dsdx * width) >> 7;
    float lrt = ((ult << 7) + dtdy * height) >> 7;

    struct GfxVertex* ul = &rsp.loaded_vertices[MAX_VERTICES + 0];
    struct GfxVertex* ll = &rsp.loaded_vertices[MAX_VERTICES + 1];
    struct GfxVertex* lr = &rsp.loaded_vertices[MAX_VERTICES + 2];
    struct GfxVertex* ur = &rsp.loaded_vertices[MAX_VERTICES + 3];
    ul->u = uls;
    ul->v = ult;
    lr->u = lrs;
    lr->v = lrt;
    if (!flip) {
        ll->u = uls;
        ll->v = lrt;
        ur->u = lrs;
        ur->v = ult;
    } else {
        ll->u = lrs;
        ll->v = ult;
        ur->u = uls;
        ur->v = lrt;
    }

    gfx_draw_rectangle(ulx, uly, lrx, lry);

    u32 cflags = rdp.combine_mode.flags;
    rdp.combine_mode = saved_combine_mode;
    rdp.combine_mode.flags = cflags;
}

static void gfx_dp_fill_rectangle(int32_t ulx, int32_t uly, int32_t lrx, int32_t lry) {
    if (rdp.color_image_address == rdp.z_buf_address) {
        // Don't clear Z buffer here since we already did it with glClear
        return;
    }
    uint32_t mode = (rdp.other_mode_h & (3U << G_MDSFT_CYCLETYPE));

    if (mode == G_CYC_COPY || mode == G_CYC_FILL) {
        // Per documentation one extra pixel is added in this modes to each edge
        lrx += 1 << 2;
        lry += 1 << 2;
    }

    for (int32_t i = MAX_VERTICES; i < MAX_VERTICES + 4; i++) {
        struct GfxVertex* v = &rsp.loaded_vertices[i];
        v->color = rdp.fill_color;
    }

    struct CombineMode saved_combine_mode = rdp.combine_mode;
    gfx_dp_set_combine_mode(
        color_comb_rgb  (G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_SHADE, 0),
        color_comb_alpha(G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_ACMUX_SHADE, 0),
        color_comb_rgb  (G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_CCMUX_SHADE, 1),
        color_comb_alpha(G_CCMUX_0, G_CCMUX_0, G_CCMUX_0, G_ACMUX_SHADE, 1));
    gfx_draw_rectangle(ulx, uly, lrx, lry);

    u32 cflags = rdp.combine_mode.flags;
    rdp.combine_mode = saved_combine_mode;
    rdp.combine_mode.flags = cflags;
}

static void gfx_dp_set_z_image(void *z_buf_address) {
    rdp.z_buf_address = z_buf_address;
}

static void gfx_dp_set_color_image(UNUSED uint32_t format, UNUSED uint32_t size, UNUSED uint32_t width, void* address) {
    rdp.color_image_address = address;
}

static void gfx_sp_set_other_mode(uint32_t shift, uint32_t num_bits, uint64_t mode) {
    uint64_t mask = (((uint64_t)1 << num_bits) - 1) << shift;
    uint64_t om = rdp.other_mode_l | ((uint64_t)rdp.other_mode_h << 32);
    om = (om & ~mask) | mode;
    rdp.other_mode_l = (uint32_t)om;
    rdp.other_mode_h = (uint32_t)(om >> 32);
}

static inline void *seg_addr(uintptr_t w1) {
    return (void *) w1;
}

#define C0(pos, width) ((cmd->words.w0 >> (pos)) & ((1U << width) - 1))
#define C1(pos, width) ((cmd->words.w1 >> (pos)) & ((1U << width) - 1))

static void OPTIMIZE_O3 gfx_run_dl(Gfx* cmd) {
    if (!cmd) { return; }

    for (;;) {
        uint32_t opcode = cmd->words.w0 >> 24;

        switch (opcode) {
            // RSP commands:
            case G_MTX:
#ifdef F3DEX_GBI_2
                gfx_sp_matrix(C0(0, 8) ^ G_MTX_PUSH, (const int32_t *) seg_addr(cmd->words.w1));
#else
                gfx_sp_matrix(C0(16, 8), (const int32_t *) seg_addr(cmd->words.w1));
#endif
                break;
            case (uint8_t)G_POPMTX:
#ifdef F3DEX_GBI_2
                gfx_sp_pop_matrix(cmd->words.w1 / 64);
#else
                gfx_sp_pop_matrix(1);
#endif
                break;
            case G_MOVEMEM:
#ifdef F3DEX_GBI_2
                gfx_sp_movemem(C0(0, 8), C0(8, 8) * 8, seg_addr(cmd->words.w1));
#else
                gfx_sp_movemem(C0(16, 8), 0, seg_addr(cmd->words.w1));
#endif
                break;
            case (uint8_t)G_MOVEWORD:
#ifdef F3DEX_GBI_2
                gfx_sp_moveword(C0(16, 8), C0(0, 16), cmd->words.w1);
#else
                gfx_sp_moveword(C0(0, 8), C0(8, 16), cmd->words.w1);
#endif
                break;
#ifdef F3DEX_GBI_2E
            case (uint8_t)G_COPYMEM:
                gfx_sp_copymem(C0(0, 8), C0(8, 8) * 8, C0(16, 8) * 8, C1(0, 8));
                break;
#endif
            case (uint8_t)G_TEXTURE:
#ifdef F3DEX_GBI_2
                gfx_sp_texture(C1(16, 16), C1(0, 16), C0(11, 3), C0(8, 3), C0(1, 7));
#else
                gfx_sp_texture(C1(16, 16), C1(0, 16), C0(11, 3), C0(8, 3), C0(0, 8));
#endif
                break;
            case G_VTX:
#ifdef F3DEX_GBI_2
                gfx_sp_vertex(C0(12, 8), C0(1, 7) - C0(12, 8), seg_addr(cmd->words.w1), true);
#elif defined(F3DEX_GBI) || defined(F3DLP_GBI)
                gfx_sp_vertex(C0(10, 6), C0(16, 8) / 2, seg_addr(cmd->words.w1), true);
#else
                gfx_sp_vertex((C0(0, 16)) / sizeof(Vtx), C0(16, 4), seg_addr(cmd->words.w1), true);
#endif
                break;
            case G_DL:
                if (C0(16, 1) == 0) {
                    // Push return address
                    gfx_run_dl((Gfx *)seg_addr(cmd->words.w1));
                } else {
                    cmd = (Gfx *)seg_addr(cmd->words.w1);
                    --cmd; // increase after break
                }
                break;
            case (uint8_t)G_ENDDL:
                return;
#ifdef F3DEX_GBI_2
            case G_GEOMETRYMODE:
                gfx_sp_geometry_mode(~C0(0, 24), cmd->words.w1);
                break;
#else
            case (uint8_t)G_SETGEOMETRYMODE:
                gfx_sp_geometry_mode(0, cmd->words.w1);
                break;
            case (uint8_t)G_CLEARGEOMETRYMODE:
                gfx_sp_geometry_mode(cmd->words.w1, 0);
                break;
#endif
            case (uint8_t)G_TRI1:
#ifdef F3DEX_GBI_2
                gfx_sp_tri1(C0(16, 8) / 2, C0(8, 8) / 2, C0(0, 8) / 2);
#elif defined(F3DEX_GBI) || defined(F3DLP_GBI)
                gfx_sp_tri1(C1(16, 8) / 2, C1(8, 8) / 2, C1(0, 8) / 2);
#else
                gfx_sp_tri1(C1(16, 8) / 10, C1(8, 8) / 10, C1(0, 8) / 10);
#endif
                break;
#if defined(F3DEX_GBI) || defined(F3DLP_GBI)
            case (uint8_t)G_TRI2:
                gfx_sp_tri1(C0(16, 8) / 2, C0(8, 8) / 2, C0(0, 8) / 2);
                gfx_sp_tri1(C1(16, 8) / 2, C1(8, 8) / 2, C1(0, 8) / 2);
                break;
#endif
            case (uint8_t)G_SETOTHERMODE_L:
#ifdef F3DEX_GBI_2
                gfx_sp_set_other_mode(31 - C0(8, 8) - C0(0, 8), C0(0, 8) + 1, cmd->words.w1);
#else
                gfx_sp_set_other_mode(C0(8, 8), C0(0, 8), cmd->words.w1);
#endif
                break;
            case (uint8_t)G_SETOTHERMODE_H:
#ifdef F3DEX_GBI_2
                gfx_sp_set_other_mode(63 - C0(8, 8) - C0(0, 8), C0(0, 8) + 1, (uint64_t) cmd->words.w1 << 32);
#else
                gfx_sp_set_other_mode(C0(8, 8) + 32, C0(0, 8), (uint64_t) cmd->words.w1 << 32);
#endif
                break;

            // RDP Commands:
            case G_SETTIMG:
                gfx_dp_set_texture_image(C0(21, 3), C0(19, 2), C0(0, 10), seg_addr(cmd->words.w1));
                break;
            case G_LOADBLOCK:
                gfx_dp_load_block(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_LOADTILE:
                gfx_dp_load_tile(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_SETTILE:
                gfx_dp_set_tile(C0(21, 3), C0(19, 2), C0(9, 9), C0(0, 9), C1(24, 3), C1(20, 4), C1(18, 2), C1(14, 4), C1(10, 4), C1(8, 2), C1(4, 4), C1(0, 4));
                break;
            case G_SETTILESIZE:
                gfx_dp_set_tile_size(C1(24, 3), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_LOADTLUT:
                gfx_dp_load_tlut(C1(24, 3), C1(14, 10));
                break;
            case G_SETENVCOLOR:
                gfx_dp_set_env_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETPRIMCOLOR:
                gfx_dp_set_prim_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETFOGCOLOR:
                gfx_dp_set_fog_color(C1(24, 8), C1(16, 8), C1(8, 8), C1(0, 8));
                break;
            case G_SETFILLCOLOR:
                gfx_dp_set_fill_color(cmd->words.w1);
                break;
            case G_SETCOMBINE:
                gfx_dp_set_combine_mode(
                    color_comb_rgb  (C0(20, 4), C1(28, 4), C0(15, 5), C1(15, 3), 0),
                    color_comb_alpha(C0(12, 3), C1(12, 3), C0(9, 3),  C1(9, 3),  0),
                    color_comb_rgb  (C0(5, 4),  C1(24, 4), C0(0, 5),  C1(6, 3),  1),
                    color_comb_alpha(C1(21, 3), C1(3, 3),  C1(18, 3), C1(0, 3),  1));
                break;
            // G_SETPRIMCOLOR, G_CCMUX_PRIMITIVE, G_ACMUX_PRIMITIVE, is used by Goddard
            // G_CCMUX_TEXEL1, LOD_FRACTION is used in Bowser room 1
            case G_TEXRECT:
            case G_TEXRECTFLIP:
            {
                int32_t lrx, lry, tile, ulx, uly;
                uint32_t uls, ult, dsdx, dtdy;
                tile = 0;
#ifdef GBI_NO_MULTI_COMMANDS
                lrx = (int32_t) (C0(13, 11) << 21) >> 19;
                lry = (int32_t) (C0(4, 9) << 23) >> 21;
                ulx = (int32_t) (C1(21, 11) << 21) >> 19;
                uly = (int32_t) (C1(12, 9) << 23) >> 21;
                uls = 0;
                ult = 0;
                dsdx = C1(4, 8) << 6;
                dtdy = (C1(0, 4) << 10) | (C0(0, 4) << 6);
#else
#ifdef F3DEX_GBI_2E
                lrx = (int32_t)(C0(0, 24) << 8) >> 8;
                lry = (int32_t)(C1(0, 24) << 8) >> 8;
                ++cmd;
                ulx = (int32_t)(C0(0, 24) << 8) >> 8;
                uly = (int32_t)(C1(0, 24) << 8) >> 8;
                ++cmd;
                uls = C0(16, 16);
                ult = C0(0, 16);
                dsdx = C1(16, 16);
                dtdy = C1(0, 16);
#else
                lrx = C0(12, 12);
                lry = C0(0, 12);
                tile = C1(24, 3);
                ulx = C1(12, 12);
                uly = C1(0, 12);
                ++cmd;
                uls = C1(16, 16);
                ult = C1(0, 16);
                ++cmd;
                dsdx = C1(16, 16);
                dtdy = C1(0, 16);
#endif
#endif
                gfx_dp_texture_rectangle(ulx, uly, lrx, lry, tile, uls, ult, dsdx, dtdy, opcode == G_TEXRECTFLIP);
                break;
            }
            case G_FILLRECT:
#ifdef GBI_NO_MULTI_COMMANDS
            {
                int32_t lrx, lry, ulx, uly;
                uly = (int32_t) (C0(12, 12) << 20) >> 18;
                lry = (int32_t) (C0(0, 12) << 20) >> 18;
                ulx = (int32_t) (C1(16, 16) << 16) >> 14;
                lrx = (int32_t) (C1(0, 16) << 16) >> 14;
                gfx_dp_fill_rectangle(ulx, uly, lrx, lry);
                break;
            }
#else
#ifdef F3DEX_GBI_2E
            {
                int32_t lrx, lry, ulx, uly;
                lrx = (int32_t)(C0(0, 24) << 8) >> 8;
                lry = (int32_t)(C1(0, 24) << 8) >> 8;
                ++cmd;
                ulx = (int32_t)(C0(0, 24) << 8) >> 8;
                uly = (int32_t)(C1(0, 24) << 8) >> 8;
                gfx_dp_fill_rectangle(ulx, uly, lrx, lry);
                break;
            }
#else
                gfx_dp_fill_rectangle(C1(12, 12), C1(0, 12), C0(12, 12), C0(0, 12));
                break;
#endif
#endif
            case G_SETSCISSOR:
                gfx_dp_set_scissor(C1(24, 2), C0(12, 12), C0(0, 12), C1(12, 12), C1(0, 12));
                break;
            case G_SETZIMG:
                gfx_dp_set_z_image(seg_addr(cmd->words.w1));
                break;
            case G_SETCIMG:
                gfx_dp_set_color_image(C0(21, 3), C0(19, 2), C0(0, 11), seg_addr(cmd->words.w1));
                break;
            default:
                ext_gfx_run_dl(cmd);
                break;
        }
        ++cmd;
    }
}

static void gfx_sp_reset(void) {
    rsp.modelview_matrix_stack_size = 1;
    rsp.current_num_lights = 2;
    rsp.lights_changed = true;
}

void gfx_get_dimensions(uint32_t *width, uint32_t *height) {
    gfx_wapi->get_dimensions(width, height);
    if (configForce4By3) {
        *width = gfx_current_dimensions.aspect_ratio * *height;
    }
}

void gfx_init(struct GfxWindowManagerAPI *wapi, struct GfxRenderingAPI *rapi, const char *window_title) {
    gfx_wapi = wapi;
    gfx_rapi = rapi;
    gfx_wapi->init(window_title);
    gfx_rapi->init();

    gfx_cc_precomp();

    gGfxInited = true;
}

struct GfxRenderingAPI *gfx_get_current_rendering_api(void) {
    return gfx_rapi;
}

void gfx_start_frame(void) {
    if (gGfxPcResetTex1 > 0) {
        gGfxPcResetTex1--;
        rdp.loaded_texture[1].addr = NULL;
        rdp.loaded_texture[1].size_bytes = 0;
    }
    gfx_wapi->handle_events();
    gfx_wapi->get_dimensions(&gfx_current_dimensions.width, &gfx_current_dimensions.height);
    if (gfx_current_dimensions.height == 0) {
        // Avoid division by zero
        gfx_current_dimensions.height = 1;
    }
    if (configForce4By3
        && ((4.0f / 3.0f) * gfx_current_dimensions.height) < gfx_current_dimensions.width) {
        gfx_current_dimensions.x_adjust_4by3 = (gfx_current_dimensions.width - (4.0f / 3.0f) * gfx_current_dimensions.height) / 2;
        gfx_current_dimensions.width = (4.0f / 3.0f) * gfx_current_dimensions.height;
    } else { gfx_current_dimensions.x_adjust_4by3 = 0; }
    gfx_current_dimensions.aspect_ratio = ((float)gfx_current_dimensions.width / (float)gfx_current_dimensions.height);
    gfx_current_dimensions.x_adjust_ratio = (4.0f / 3.0f) / gfx_current_dimensions.aspect_ratio;
}

void gfx_run(Gfx *commands) {
    gfx_sp_reset();

    //puts("New frame");

    if (!gfx_wapi->start_frame()) {
        dropped_frame = true;
        return;
    }
    dropped_frame = false;

    //double t0 = gfx_wapi->get_time();
    gfx_rapi->start_frame();
    gfx_run_dl(commands);
    gfx_flush();
    //double t1 = gfx_wapi->get_time();
    //printf("Process %f %f\n", t1, t1 - t0);
    gfx_rapi->end_frame();
    gfx_wapi->swap_buffers_begin();
}

void gfx_end_frame(void) {
    if (!dropped_frame) {
        gfx_rapi->finish_render();
        gfx_wapi->swap_buffers_end();
    }
}

void gfx_shutdown(void) {
    if (gfx_rapi) {
        if (gfx_rapi->shutdown) gfx_rapi->shutdown();
        gfx_rapi = NULL;
    }
    if (gfx_wapi) {
        if (gfx_wapi->shutdown) gfx_wapi->shutdown();
        gfx_wapi = NULL;
    }
    gGfxInited = false;
}

  /////////////////////////
 // v custom for djui v //
/////////////////////////

static bool    sDjuiClip          = 0;
static uint8_t sDjuiClipX1        = 0;
static uint8_t sDjuiClipY1        = 0;
static uint8_t sDjuiClipX2        = 0;
static uint8_t sDjuiClipY2        = 0;

static bool    sDjuiOverride        = false;
static void*   sDjuiOverrideTexture = NULL;
static uint32_t sDjuiOverrideW       = 0;
static uint32_t sDjuiOverrideH       = 0;
static uint32_t sDjuiOverrideB       = 0;

static void OPTIMIZE_O3 djui_gfx_dp_execute_clipping(void) {
    if (!sDjuiClip) { return; }
    sDjuiClip = false;

    size_t start_index = 0;
    size_t dest_index = 4;

    float minX = rsp.loaded_vertices[start_index].x;
    float maxX = rsp.loaded_vertices[start_index].x;
    float minY = rsp.loaded_vertices[start_index].y;
    float maxY = rsp.loaded_vertices[start_index].y;

    float minU = rsp.loaded_vertices[start_index].u;
    float maxU = rsp.loaded_vertices[start_index].u;
    float minV = rsp.loaded_vertices[start_index].v;
    float maxV = rsp.loaded_vertices[start_index].v;

    for (size_t i = start_index; i < dest_index; i++) {
        struct GfxVertex* d = &rsp.loaded_vertices[i];
        minX = fmin(minX, d->x);
        maxX = fmax(maxX, d->x);
        minY = fmin(minY, d->y);
        maxY = fmax(maxY, d->y);

        minU = fmin(minU, d->u);
        maxU = fmax(maxU, d->u);
        minV = fmin(minV, d->v);
        maxV = fmax(maxV, d->v);
    }

    float midY = (minY + maxY) / 2.0f;
    float midX = (minX + maxX) / 2.0f;
    float midU = (minU + maxU) / 2.0f;
    float midV = (minV + maxV) / 2.0f;
    for (size_t i = start_index; i < dest_index; i++) {
        struct GfxVertex* d = &rsp.loaded_vertices[i];
        if (d->x <= midX) {
            d->x += (maxX - minX) * (sDjuiClipX1 / 255.0f);
        } else {
            d->x -= (maxX - minX) * (sDjuiClipX2 / 255.0f);
        }
        if (d->y <= midY) {
            d->y += (maxY - minY) * (sDjuiClipY2 / 255.0f);
        } else {
            d->y -= (maxY - minY) * (sDjuiClipY1 / 255.0f);
        }

        if (d->u <= midU) {
            d->u += (maxU - minU) * (sDjuiClipX1 / 255.0f);
        } else {
            d->u -= (maxU - minU) * (sDjuiClipX2 / 255.0f);
        }
        if (d->v <= midV) {
            d->v += (maxV - minV) * (sDjuiClipY1 / 255.0f);
        } else {
            d->v -= (maxV - minV) * (sDjuiClipY2 / 255.0f);
        }
    }
}

static void OPTIMIZE_O3 djui_gfx_dp_execute_override(void) {
    if (!sDjuiOverride) { return; }
    sDjuiOverride = false;

    // gsDPSetTextureImage
    uint8_t sizeLoadBlock = (sDjuiOverrideB == 32) ? 3 : 2;
    rdp.texture_to_load.addr = sDjuiOverrideTexture;
    rdp.texture_to_load.siz = sizeLoadBlock;

    // gsDPSetTile
    rdp.texture_tile.siz = sizeLoadBlock;

    // gsDPLoadBlock
    uint32_t wordSizeShift = (sDjuiOverrideB == 32) ? 2 : 1;
    uint32_t lrs = (sDjuiOverrideW * sDjuiOverrideH) - 1;
    uint32_t sizeBytes = (lrs + 1) << wordSizeShift;
    gfx_update_loaded_texture(rdp.texture_to_load.tile_number, sizeBytes, rdp.texture_to_load.addr);

    // gsDPSetTile
    uint32_t line = (((sDjuiOverrideW * 2) + 7) >> 3);
    rdp.texture_tile.line_size_bytes = line * 8;

    // gsDPSetTileSize
    /*rdp.texture_tile.uls = 0;
    rdp.texture_tile.ult = 0;
    rdp.texture_tile.lrs = (sDjuiOverrideW - 1) << G_TEXTURE_IMAGE_FRAC;
    rdp.texture_tile.lrt = (sDjuiOverrideH - 1) << G_TEXTURE_IMAGE_FRAC;*/
}

static void OPTIMIZE_O3 djui_gfx_dp_execute_djui(uint32_t opcode) {
    switch (opcode) {
        case G_TEXOVERRIDE_DJUI: djui_gfx_dp_execute_override(); break;
        case G_TEXCLIP_DJUI:     djui_gfx_dp_execute_clipping(); break;
    }
}

static void OPTIMIZE_O3 djui_gfx_dp_set_clipping(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2) {
    sDjuiClipX1 = x1;
    sDjuiClipY1 = y1;
    sDjuiClipX2 = x2;
    sDjuiClipY2 = y2;
    sDjuiClip   = true;
}

static void OPTIMIZE_O3 djui_gfx_dp_set_override(void* texture, uint32_t w, uint32_t h, uint32_t b) {
    sDjuiOverrideTexture = texture;
    sDjuiOverrideW = w;
    sDjuiOverrideH = h;
    sDjuiOverrideB = b;
    sDjuiOverride  = (texture != NULL);
}

/*static void OPTIMIZE_O3 djui_gfx_sp_simple_vertex(size_t n_vertices, size_t dest_index, const Vtx *vertices) {
    gfx_sp_vertex(n_vertices, dest_index, vertices, false);
    return;
}*/

static void OPTIMIZE_O3 djui_gfx_sp_simple_tri1(uint8_t vtx1_idx, uint8_t vtx2_idx, uint8_t vtx3_idx) {
    gfx_sp_tri1(vtx1_idx, vtx2_idx, vtx3_idx);
    return;
}

void gfx_pc_precomp_shader(uint32_t rgb1, uint32_t alpha1, uint32_t rgb2, uint32_t alpha2, uint32_t flags) {
    gfx_dp_set_combine_mode(rgb1, alpha1, rgb2, alpha2);

    struct CombineMode* cm = &rdp.combine_mode;
    cm->flags = flags;

    gfx_lookup_or_create_color_combiner(cm);
}

void OPTIMIZE_O3 ext_gfx_run_dl(Gfx* cmd) {
    uint32_t opcode = cmd->words.w0 >> 24;
    switch (opcode) {
        case G_TEXCLIP_DJUI:
            djui_gfx_dp_set_clipping(C0(16, 8), C0(8, 8), C1(16, 8), C1(8, 8));
            break;
        case G_TEXOVERRIDE_DJUI:
            djui_gfx_dp_set_override(seg_addr(cmd->words.w1), 1 << C0(16, 8), 1 << C0(8, 8), C0(0, 8));
            break;
        case G_VTX_EXT:
#ifdef F3DEX_GBI_2
            gfx_sp_vertex(C0(12, 8), C0(1, 7) - C0(12, 8), seg_addr(cmd->words.w1), false);
#elif defined(F3DEX_GBI) || defined(F3DLP_GBI)
            gfx_sp_vertex(C0(10, 6), C0(16, 8) / 2, seg_addr(cmd->words.w1), false);
#else
            gfx_sp_vertex((C0(0, 16)) / sizeof(Vtx), C0(16, 4), seg_addr(cmd->words.w1), false);
#endif
            break;
        case G_TRI2_EXT:
            djui_gfx_sp_simple_tri1(C0(16, 8) / 2, C0(8, 8) / 2, C0(0, 8) / 2);
            djui_gfx_sp_simple_tri1(C1(16, 8) / 2, C1(8, 8) / 2, C1(0, 8) / 2);
            break;
        case G_TEXADDR_DJUI:
            sOnlyTextureChangeOnAddrChange = !(C0(0, 24) & 0x01);
            break;
        case G_EXECUTE_DJUI:
            djui_gfx_dp_execute_djui(cmd->words.w1);
            break;
    }
}
