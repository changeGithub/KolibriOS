/**************************************************************************

Copyright 2001 VA Linux Systems Inc., Fremont, California.
Copyright � 2002 by David Dawes

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
on the rights to use, copy, modify, merge, publish, distribute, sub
license, and/or sell copies of the Software, and to permit persons to whom
the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
THE COPYRIGHT HOLDERS AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

/*
 * Authors: Jeff Hartmann <jhartmann@valinux.com>
 *          Abraham van der Merwe <abraham@2d3d.co.za>
 *          David Dawes <dawes@xfree86.org>
 *          Alan Hourihane <alanh@tungstengraphics.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <memory.h>
#include <malloc.h>
#include <kos32sys.h>
#include <pixlib2.h>
#include "i915_pciids.h"

#include "compiler.h"
#include "sna.h"
#include "intel_driver.h"

#define to_surface(x) (surface_t*)((x)->handle)

static struct sna_fb sna_fb;
static int    tls_mask;

int tls_alloc(void);

static inline void *tls_get(int key)
{
    void *val;
    __asm__ __volatile__(
    "movl %%fs:(%1), %0"
    :"=r"(val)
    :"r"(key));

  return val;
};

static inline int
tls_set(int key, const void *ptr)
{
    if(!(key & 3))
    {
        __asm__ __volatile__(
        "movl %0, %%fs:(%1)"
        ::"r"(ptr),"r"(key));
        return 0;
    }
    else return -1;
}




int kgem_init_fb(struct kgem *kgem, struct sna_fb *fb);
int kgem_update_fb(struct kgem *kgem, struct sna_fb *fb);
uint32_t kgem_surface_size(struct kgem *kgem,bool relaxed_fencing,
				  unsigned flags, uint32_t width, uint32_t height,
				  uint32_t bpp, uint32_t tiling, uint32_t *pitch);
struct kgem_bo *kgem_bo_from_handle(struct kgem *kgem, int handle,
                        int pitch, int height);

void kgem_close_batches(struct kgem *kgem);
void sna_bo_destroy(struct kgem *kgem, struct kgem_bo *bo);

const struct intel_device_info *
intel_detect_chipset(struct pci_device *pci);

static bool sna_solid_cache_init(struct sna *sna);

struct sna *sna_device;

__LOCK_INIT_RECURSIVE(, __sna_lock);

static void no_render_reset(struct sna *sna)
{
	(void)sna;
}

static void no_render_flush(struct sna *sna)
{
	(void)sna;
}

static void
no_render_context_switch(struct kgem *kgem,
			 int new_mode)
{
	if (!kgem->nbatch)
		return;

	if (kgem_ring_is_idle(kgem, kgem->ring)) {
		DBG(("%s: GPU idle, flushing\n", __FUNCTION__));
		_kgem_submit(kgem);
	}

	(void)new_mode;
}

static void
no_render_retire(struct kgem *kgem)
{
	(void)kgem;
}

static void
no_render_expire(struct kgem *kgem)
{
	(void)kgem;
}

static void
no_render_fini(struct sna *sna)
{
	(void)sna;
}

const char *no_render_init(struct sna *sna)
{
    struct sna_render *render = &sna->render;

    memset (render,0, sizeof (*render));

    render->prefer_gpu = PREFER_GPU_BLT;

    render->vertices = render->vertex_data;
    render->vertex_size = ARRAY_SIZE(render->vertex_data);

    render->reset = no_render_reset;
	render->flush = no_render_flush;
	render->fini = no_render_fini;

	sna->kgem.context_switch = no_render_context_switch;
	sna->kgem.retire = no_render_retire;
	sna->kgem.expire = no_render_expire;

	sna->kgem.mode = KGEM_RENDER;
	sna->kgem.ring = KGEM_RENDER;

	sna_vertex_init(sna);
	return "generic";
 }

void sna_vertex_init(struct sna *sna)
{
//    pthread_mutex_init(&sna->render.lock, NULL);
//    pthread_cond_init(&sna->render.wait, NULL);
    sna->render.active = 0;
}

int sna_accel_init(struct sna *sna)
{
    const char *backend;

	backend = no_render_init(sna);
	if (sna->info->gen >= 0100)
		(void)backend;
	else if (sna->info->gen >= 070)
		backend = gen7_render_init(sna, backend);
	else if (sna->info->gen >= 060)
		backend = gen6_render_init(sna, backend);
	else if (sna->info->gen >= 050)
		backend = gen5_render_init(sna, backend);
	else if (sna->info->gen >= 040)
		backend = gen4_render_init(sna, backend);
	else if (sna->info->gen >= 030)
		backend = gen3_render_init(sna, backend);

	DBG(("%s(backend=%s, prefer_gpu=%x)\n",
	     __FUNCTION__, backend, sna->render.prefer_gpu));

	kgem_reset(&sna->kgem);

    sna_device = sna;

    return kgem_init_fb(&sna->kgem, &sna_fb);
}

int sna_init(uint32_t service)
{
    ioctl_t   io;
    int caps = 0;

    static struct pci_device device;
    struct sna *sna;

    DBG(("%s\n", __FUNCTION__));

    __lock_acquire_recursive(__sna_lock);

    if(sna_device)
        goto done;

    io.handle   = service;
    io.io_code  = SRV_GET_PCI_INFO;
    io.input    = &device;
    io.inp_size = sizeof(device);
    io.output   = NULL;
    io.out_size = 0;

    if (call_service(&io)!=0)
        goto err1;

    sna = malloc(sizeof(*sna));
    if (sna == NULL)
        goto err1;

    memset(sna, 0, sizeof(*sna));

    sna->cpu_features = sna_cpu_detect();

    sna->PciInfo = &device;
  	sna->info = intel_detect_chipset(sna->PciInfo);
    sna->scrn = service;

    kgem_init(&sna->kgem, service, sna->PciInfo, sna->info->gen);


    /* Disable tiling by default */
    sna->tiling = 0;

    /* Default fail-safe value of 75 Hz */
//    sna->vblank_interval = 1000 * 1000 * 1000 / 75;

    sna->flags = 0;

    sna_accel_init(sna);

    tls_mask = tls_alloc();

//    printf("tls mask %x\n", tls_mask);

done:
    caps = sna_device->render.caps;

err1:
    __lock_release_recursive(__sna_lock);

    return caps;
}

void sna_fini()
{
    if( sna_device )
    {
        struct kgem_bo *mask;

        __lock_acquire_recursive(__sna_lock);

        mask = tls_get(tls_mask);

        sna_device->render.fini(sna_device);
        if(mask)
            kgem_bo_destroy(&sna_device->kgem, mask);
        kgem_close_batches(&sna_device->kgem);
   	    kgem_cleanup_cache(&sna_device->kgem);

   	    sna_device = NULL;
        __lock_release_recursive(__sna_lock);
    };
}

#if 0

static bool sna_solid_cache_init(struct sna *sna)
{
    struct sna_solid_cache *cache = &sna->render.solid_cache;

    DBG(("%s\n", __FUNCTION__));

    cache->cache_bo =
        kgem_create_linear(&sna->kgem, sizeof(cache->color));
    if (!cache->cache_bo)
        return FALSE;

    /*
     * Initialise [0] with white since it is very common and filling the
     * zeroth slot simplifies some of the checks.
     */
    cache->color[0] = 0xffffffff;
    cache->bo[0] = kgem_create_proxy(cache->cache_bo, 0, sizeof(uint32_t));
    cache->bo[0]->pitch = 4;
    cache->dirty = 1;
    cache->size = 1;
    cache->last = 0;

    return TRUE;
}

void
sna_render_flush_solid(struct sna *sna)
{
    struct sna_solid_cache *cache = &sna->render.solid_cache;

    DBG(("sna_render_flush_solid(size=%d)\n", cache->size));
    assert(cache->dirty);
    assert(cache->size);

    kgem_bo_write(&sna->kgem, cache->cache_bo,
              cache->color, cache->size*sizeof(uint32_t));
    cache->dirty = 0;
    cache->last = 0;
}

static void
sna_render_finish_solid(struct sna *sna, bool force)
{
    struct sna_solid_cache *cache = &sna->render.solid_cache;
    int i;

    DBG(("sna_render_finish_solid(force=%d, domain=%d, busy=%d, dirty=%d)\n",
         force, cache->cache_bo->domain, cache->cache_bo->rq != NULL, cache->dirty));

    if (!force && cache->cache_bo->domain != DOMAIN_GPU)
        return;

    if (cache->dirty)
        sna_render_flush_solid(sna);

    for (i = 0; i < cache->size; i++) {
        if (cache->bo[i] == NULL)
            continue;

        kgem_bo_destroy(&sna->kgem, cache->bo[i]);
        cache->bo[i] = NULL;
    }
    kgem_bo_destroy(&sna->kgem, cache->cache_bo);

    DBG(("sna_render_finish_solid reset\n"));

    cache->cache_bo = kgem_create_linear(&sna->kgem, sizeof(cache->color));
    cache->bo[0] = kgem_create_proxy(cache->cache_bo, 0, sizeof(uint32_t));
    cache->bo[0]->pitch = 4;
    if (force)
        cache->size = 1;
}


struct kgem_bo *
sna_render_get_solid(struct sna *sna, uint32_t color)
{
    struct sna_solid_cache *cache = &sna->render.solid_cache;
    int i;

    DBG(("%s: %08x\n", __FUNCTION__, color));

//    if ((color & 0xffffff) == 0) /* alpha only */
//        return kgem_bo_reference(sna->render.alpha_cache.bo[color>>24]);

    if (color == 0xffffffff) {
        DBG(("%s(white)\n", __FUNCTION__));
        return kgem_bo_reference(cache->bo[0]);
    }

    if (cache->color[cache->last] == color) {
        DBG(("sna_render_get_solid(%d) = %x (last)\n",
             cache->last, color));
        return kgem_bo_reference(cache->bo[cache->last]);
    }

    for (i = 1; i < cache->size; i++) {
        if (cache->color[i] == color) {
            if (cache->bo[i] == NULL) {
                DBG(("sna_render_get_solid(%d) = %x (recreate)\n",
                     i, color));
                goto create;
            } else {
                DBG(("sna_render_get_solid(%d) = %x (old)\n",
                     i, color));
                goto done;
            }
        }
    }

    sna_render_finish_solid(sna, i == ARRAY_SIZE(cache->color));

    i = cache->size++;
    cache->color[i] = color;
    cache->dirty = 1;
    DBG(("sna_render_get_solid(%d) = %x (new)\n", i, color));

create:
    cache->bo[i] = kgem_create_proxy(cache->cache_bo,
                     i*sizeof(uint32_t), sizeof(uint32_t));
    cache->bo[i]->pitch = 4;

done:
    cache->last = i;
    return kgem_bo_reference(cache->bo[i]);
}

#endif


int sna_blit_copy(bitmap_t *src_bitmap, int dst_x, int dst_y,
                  int w, int h, int src_x, int src_y)

{
    struct sna_copy_op copy;
    struct _Pixmap src, dst;
    struct kgem_bo *src_bo;

    char proc_info[1024];
    int winx, winy;

    get_proc_info(proc_info);

    winx = *(uint32_t*)(proc_info+34);
    winy = *(uint32_t*)(proc_info+38);

    memset(&src, 0, sizeof(src));
    memset(&dst, 0, sizeof(dst));

    src.drawable.bitsPerPixel = 32;
    src.drawable.width  = src_bitmap->width;
    src.drawable.height = src_bitmap->height;

    dst.drawable.bitsPerPixel = 32;
    dst.drawable.width  = sna_fb.width;
    dst.drawable.height = sna_fb.height;

    memset(&copy, 0, sizeof(copy));

    src_bo = (struct kgem_bo*)src_bitmap->handle;

    if( sna_device->render.copy(sna_device, GXcopy,
                                &src, src_bo,
                                &dst, sna_fb.fb_bo, &copy) )
    {
        copy.blt(sna_device, &copy, src_x, src_y, w, h, winx+dst_x, winy+dst_y);
        copy.done(sna_device, &copy);
    }

    kgem_submit(&sna_device->kgem);

    return 0;

//    __asm__ __volatile__("int3");

};

typedef struct
{
    uint32_t        width;
    uint32_t        height;
    void           *data;
    uint32_t        pitch;
    struct kgem_bo *bo;
    uint32_t        bo_size;
    uint32_t        flags;
}surface_t;



int sna_create_bitmap(bitmap_t *bitmap)
{
    surface_t *sf;
	struct kgem_bo *bo;

    sf = malloc(sizeof(*sf));
    if(sf == NULL)
        goto err_1;

    __lock_acquire_recursive(__sna_lock);

    bo = kgem_create_2d(&sna_device->kgem, bitmap->width, bitmap->height,
                        32,I915_TILING_NONE, CREATE_CPU_MAP);

    if(bo == NULL)
        goto err_2;

    void *map = kgem_bo_map(&sna_device->kgem, bo);
    if(map == NULL)
        goto err_3;

    sf->width   = bitmap->width;
    sf->height  = bitmap->height;
    sf->data    = map;
    sf->pitch   = bo->pitch;
    sf->bo      = bo;
    sf->bo_size = PAGE_SIZE * bo->size.pages.count;
    sf->flags   = bitmap->flags;

    bitmap->handle = (uint32_t)sf;
    __lock_release_recursive(__sna_lock);

    return 0;

err_3:
    kgem_bo_destroy(&sna_device->kgem, bo);
err_2:
    __lock_release_recursive(__sna_lock);
    free(sf);
err_1:
    return -1;
};

int sna_bitmap_from_handle(bitmap_t *bitmap, uint32_t handle)
{
    surface_t *sf;
	struct kgem_bo *bo;

    sf = malloc(sizeof(*sf));
    if(sf == NULL)
        goto err_1;

    __lock_acquire_recursive(__sna_lock);

    bo = kgem_bo_from_handle(&sna_device->kgem, handle, bitmap->pitch, bitmap->height);

    __lock_release_recursive(__sna_lock);

    sf->width   = bitmap->width;
    sf->height  = bitmap->height;
    sf->data    = NULL;
    sf->pitch   = bo->pitch;
    sf->bo      = bo;
    sf->bo_size = PAGE_SIZE * bo->size.pages.count;
    sf->flags   = bitmap->flags;

    bitmap->handle = (uint32_t)sf;

    return 0;

err_2:
    __lock_release_recursive(__sna_lock);
    free(sf);
err_1:
    return -1;
};

void sna_set_bo_handle(bitmap_t *bitmap, int handle)
{
    surface_t *sf = to_surface(bitmap);
    struct kgem_bo *bo = sf->bo;
    bo->handle = handle;
}

int sna_destroy_bitmap(bitmap_t *bitmap)
{
    surface_t *sf = to_surface(bitmap);

    __lock_acquire_recursive(__sna_lock);

    kgem_bo_destroy(&sna_device->kgem, sf->bo);

    __lock_release_recursive(__sna_lock);

    free(sf);

    bitmap->handle = -1;
    bitmap->data   = (void*)-1;
    bitmap->pitch  = -1;

    return 0;
};

int sna_lock_bitmap(bitmap_t *bitmap)
{
    surface_t *sf = to_surface(bitmap);

//    printf("%s\n", __FUNCTION__);
    __lock_acquire_recursive(__sna_lock);

    kgem_bo_sync__cpu(&sna_device->kgem, sf->bo);

    __lock_release_recursive(__sna_lock);

    bitmap->data  = sf->data;
    bitmap->pitch = sf->pitch;

    return 0;
};

int sna_resize_bitmap(bitmap_t *bitmap)
{
    surface_t *sf = to_surface(bitmap);
    struct kgem *kgem = &sna_device->kgem;
    struct kgem_bo *bo = sf->bo;

    uint32_t   size;
    uint32_t   pitch;

   	bitmap->pitch = -1;
    bitmap->data = (void *) -1;

	size = kgem_surface_size(kgem,kgem->has_relaxed_fencing, CREATE_CPU_MAP,
				 bitmap->width, bitmap->height, 32, I915_TILING_NONE, &pitch);
	assert(size && size <= kgem->max_object_size);

    if(sf->bo_size >= size)
    {
        sf->width   = bitmap->width;
        sf->height  = bitmap->height;
        sf->pitch   = pitch;
        bo->pitch   = pitch;

	    return 0;
    }
    else
    {
        __lock_acquire_recursive(__sna_lock);

        sna_bo_destroy(kgem, bo);

        sf->bo = NULL;

        bo = kgem_create_2d(kgem, bitmap->width, bitmap->height,
                            32, I915_TILING_NONE, CREATE_CPU_MAP);

        if(bo == NULL)
        {
            __lock_release_recursive(__sna_lock);
            return -1;
        };

        void *map = kgem_bo_map(kgem, bo);
        if(map == NULL)
        {
            sna_bo_destroy(kgem, bo);
            __lock_release_recursive(__sna_lock);
            return -1;
        };

        __lock_release_recursive(__sna_lock);

        sf->width   = bitmap->width;
        sf->height  = bitmap->height;
        sf->data    = map;
        sf->pitch   = bo->pitch;
        sf->bo      = bo;
        sf->bo_size = PAGE_SIZE * bo->size.pages.count;
    }

    return 0;
};



int sna_create_mask()
{
	struct kgem_bo *bo;

//    printf("%s width %d height %d\n", __FUNCTION__, sna_fb.width, sna_fb.height);

    __lock_acquire_recursive(__sna_lock);

    bo = kgem_create_2d(&sna_device->kgem, sna_fb.width, sna_fb.height,
                        8,I915_TILING_NONE, CREATE_CPU_MAP);

    if(unlikely(bo == NULL))
        goto err_1;

    int *map = kgem_bo_map(&sna_device->kgem, bo);
    if(map == NULL)
        goto err_2;

    __lock_release_recursive(__sna_lock);

    memset(map, 0, bo->pitch * sna_fb.height);

    tls_set(tls_mask, bo);

    return 0;

err_2:
    kgem_bo_destroy(&sna_device->kgem, bo);
err_1:
    __lock_release_recursive(__sna_lock);
    return -1;
};


bool
gen6_composite(struct sna *sna,
              uint8_t op,
		      PixmapPtr src, struct kgem_bo *src_bo,
		      PixmapPtr mask,struct kgem_bo *mask_bo,
		      PixmapPtr dst, struct kgem_bo *dst_bo,
              int32_t src_x, int32_t src_y,
              int32_t msk_x, int32_t msk_y,
              int32_t dst_x, int32_t dst_y,
              int32_t width, int32_t height,
              struct sna_composite_op *tmp);


#define MAP(ptr) ((void*)((uintptr_t)(ptr) & ~3))

int sna_blit_tex(bitmap_t *bitmap, bool scale, int dst_x, int dst_y,
                  int w, int h, int src_x, int src_y)

{
    surface_t *sf = to_surface(bitmap);

    struct drm_i915_mask_update update;

    struct sna_composite_op composite;
    struct _Pixmap src, dst, mask;
    struct kgem_bo *src_bo, *mask_bo;
    int winx, winy;

    char proc_info[1024];

    get_proc_info(proc_info);

    winx = *(uint32_t*)(proc_info+34);
    winy = *(uint32_t*)(proc_info+38);
//    winw = *(uint32_t*)(proc_info+42)+1;
//    winh = *(uint32_t*)(proc_info+46)+1;

    mask_bo = tls_get(tls_mask);

    if(unlikely(mask_bo == NULL))
    {
        sna_create_mask();
        mask_bo = tls_get(tls_mask);
        if( mask_bo == NULL)
            return -1;
    };

    if(kgem_update_fb(&sna_device->kgem, &sna_fb))
    {
        __lock_acquire_recursive(__sna_lock);
        kgem_bo_destroy(&sna_device->kgem, mask_bo);
        __lock_release_recursive(__sna_lock);

        sna_create_mask();
        mask_bo = tls_get(tls_mask);
        if( mask_bo == NULL)
            return -1;
    }

    VG_CLEAR(update);
	update.handle = mask_bo->handle;
	update.bo_map = (int)kgem_bo_map__cpu(&sna_device->kgem, mask_bo);
	drmIoctl(sna_device->kgem.fd, SRV_MASK_UPDATE, &update);
    mask_bo->pitch = update.bo_pitch;

    memset(&src, 0, sizeof(src));
    memset(&dst, 0, sizeof(dst));
    memset(&mask, 0, sizeof(dst));

    src.drawable.bitsPerPixel = 32;

    src.drawable.width  = sf->width;
    src.drawable.height = sf->height;

    dst.drawable.bitsPerPixel = 32;
    dst.drawable.width  = sna_fb.width;
    dst.drawable.height = sna_fb.height;

    mask.drawable.bitsPerPixel = 8;
    mask.drawable.width  = update.width;
    mask.drawable.height = update.height;

    memset(&composite, 0, sizeof(composite));

    src_bo = sf->bo;

    __lock_acquire_recursive(__sna_lock);


    if( sna_device->render.blit_tex(sna_device, PictOpSrc,scale,
		      &src, src_bo,
		      &mask, mask_bo,
		      &dst, sna_fb.fb_bo,
              src_x, src_y,
              dst_x, dst_y,
              winx+dst_x, winy+dst_y,
              w, h,
              &composite) )
    {
	    struct sna_composite_rectangles r;

	    r.src.x = src_x;
	    r.src.y = src_y;
	    r.mask.x = dst_x;
	    r.mask.y = dst_y;
		r.dst.x = winx+dst_x;
	    r.dst.y = winy+dst_y;
	    r.width  = w;
	    r.height = h;

        composite.blt(sna_device, &composite, &r);
        composite.done(sna_device, &composite);

    };

    kgem_submit(&sna_device->kgem);

    __lock_release_recursive(__sna_lock);

    bitmap->data   = (void*)-1;
    bitmap->pitch  = -1;

    return 0;
}







static const struct intel_device_info intel_generic_info = {
	.gen = -1,
};

static const struct intel_device_info intel_i915_info = {
	.gen = 030,
};
static const struct intel_device_info intel_i945_info = {
	.gen = 031,
};

static const struct intel_device_info intel_g33_info = {
	.gen = 033,
};

static const struct intel_device_info intel_i965_info = {
	.gen = 040,
};

static const struct intel_device_info intel_g4x_info = {
	.gen = 045,
};

static const struct intel_device_info intel_ironlake_info = {
	.gen = 050,
};

static const struct intel_device_info intel_sandybridge_info = {
	.gen = 060,
};

static const struct intel_device_info intel_ivybridge_info = {
	.gen = 070,
};

static const struct intel_device_info intel_valleyview_info = {
	.gen = 071,
};

static const struct intel_device_info intel_haswell_info = {
	.gen = 075,
};

#define INTEL_DEVICE_MATCH(d,i) \
    { 0x8086, (d), PCI_MATCH_ANY, PCI_MATCH_ANY, 0x3 << 16, 0xff << 16, (intptr_t)(i) }


static const struct pci_id_match intel_device_match[] = {

	INTEL_I915G_IDS(&intel_i915_info),
	INTEL_I915GM_IDS(&intel_i915_info),
	INTEL_I945G_IDS(&intel_i945_info),
	INTEL_I945GM_IDS(&intel_i945_info),

	INTEL_G33_IDS(&intel_g33_info),
	INTEL_PINEVIEW_IDS(&intel_g33_info),

	INTEL_I965G_IDS(&intel_i965_info),
	INTEL_I965GM_IDS(&intel_i965_info),

	INTEL_G45_IDS(&intel_g4x_info),
	INTEL_GM45_IDS(&intel_g4x_info),

	INTEL_IRONLAKE_D_IDS(&intel_ironlake_info),
	INTEL_IRONLAKE_M_IDS(&intel_ironlake_info),

	INTEL_SNB_D_IDS(&intel_sandybridge_info),
	INTEL_SNB_M_IDS(&intel_sandybridge_info),

	INTEL_IVB_D_IDS(&intel_ivybridge_info),
	INTEL_IVB_M_IDS(&intel_ivybridge_info),

	INTEL_HSW_D_IDS(&intel_haswell_info),
	INTEL_HSW_M_IDS(&intel_haswell_info),

	INTEL_VLV_D_IDS(&intel_valleyview_info),
	INTEL_VLV_M_IDS(&intel_valleyview_info),

	INTEL_VGA_DEVICE(PCI_MATCH_ANY, &intel_generic_info),

	{ 0, 0, 0 },
};

const struct pci_id_match *PciDevMatch(uint16_t dev,const struct pci_id_match *list)
{
    while(list->device_id)
    {
        if(dev==list->device_id)
            return list;
        list++;
    }
    return NULL;
}

const struct intel_device_info *
intel_detect_chipset(struct pci_device *pci)
{
    const struct pci_id_match *ent = NULL;

    ent = PciDevMatch(pci->device_id, intel_device_match);

    if(ent != NULL)
        return (const struct intel_device_info*)ent->match_data;
    else
        return &intel_generic_info;

#if 0
	for (i = 0; intel_chipsets[i].name != NULL; i++) {
		if (DEVICE_ID(pci) == intel_chipsets[i].token) {
			name = intel_chipsets[i].name;
			break;
		}
	}
	if (name == NULL) {
		xf86DrvMsg(scrn->scrnIndex, X_WARNING, "unknown chipset\n");
		name = "unknown";
	} else {
		xf86DrvMsg(scrn->scrnIndex, from,
			   "Integrated Graphics Chipset: Intel(R) %s\n",
			   name);
	}

	scrn->chipset = name;
#endif

}

int intel_get_device_id(int fd)
{
	struct drm_i915_getparam gp;
	int devid = 0;

	memset(&gp, 0, sizeof(gp));
	gp.param = I915_PARAM_CHIPSET_ID;
	gp.value = &devid;

	if (drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
		return 0;

	return devid;
}

int drmIoctl(int fd, unsigned long request, void *arg)
{
    ioctl_t  io;

    io.handle   = fd;
    io.io_code  = request;
    io.input    = arg;
    io.inp_size = 64;
    io.output   = NULL;
    io.out_size = 0;

    return call_service(&io);
}



