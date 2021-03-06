
export CC = kos32-gcc
export AR = kos32-ar
export LD = kos32-ld
export STRIP = kos32-strip

export SDK_DIR:= $(abspath ../../..)

EGL = egl

CFLAGS_OPT = -U_Win32 -U_WIN32 -U__MINGW32__ -UWIN32 -U_MSC_VER -Os 
CFLAGS_OPT+= -fomit-frame-pointer -fno-ident -mno-ms-bitfields
CFLAGS   = -c $(CFLAGS_OPT)

LDFLAGS = -nostdlib -shared -s --image-base 0 -T ../../newlib/dll.lds -e _DllStartup
LDFLAGS+= --out-implib lib$(EGL).dll.a 

INC_MESA=  -I../../newlib/libc/include -I./include -I./src -I./src/glsl -I./src/mesa -I./src/mapi
INC_EGL=   -I../../newlib/libc/include -I../../../includes -I./include -I../../libdrm -I../../libdrm/include/drm -I./src/egl/main -I./src/gbm/backends/dri -I./src/gbm/main

LIBPATH:= -L../../../lib -L/home/autobuild/tools/win32/mingw32/lib

LIBS:=  -ldll -ldrm.dll -lc.dll -lgcc

MESA_DEFS= -DMESA_DLL -DBUILD_GL32 -DMAPI_MODE_UTIL -DMAPI_TABLE_NUM_STATIC=87 -DUSE_X86_ASM -DUSE_MMX_ASM -DUSE_SSE_ASM -DMAPI_TABLE_NUM_DYNAMIC=256
MESA_DEFS+= -DPACKAGE_NAME=\"Mesa\" -DPACKAGE_VERSION=\"9.2.5\" -DPACKAGE_BUGREPORT=\"https://bugs.freedesktop.org/enter_bug.cgi\?product=Mesa\"
MESA_DEFS+= -DPACKAGE_URL=\"\" -DPACKAGE=\"mesa\" -DVERSION=\"9.2.5\" -DSTDC_HEADERS=1 -DHAVE_SYS_TYPES_H=1 -DHAVE_STDLIB_H=1 -DHAVE_STRING_H=1
MESA_DEFS+= -DHAVE_MEMORY_H=1 -DHAVE_STRINGS_H=1 -DHAVE_INTTYPES_H=1 -DHAVE_STDINT_H=1

EGL_DEFS = -DHAVE_DRM_PLATFORM -D__unix__ -DMESA_EGL_NO_X11_HEADERS -D_EGL_BUILT_IN_DRIVER_DRI2

SUBDIRS = src/glsl src/mapi src/gallium src/egl src/mesa

# targets 

all:
	@echo $(SDK_DIR);	\
	for i in $(SUBDIRS); do	\
	$(MAKE) -C $$i;		\
	done
 
$(EGL).dll: $(EGL_OBJS) Makefile
	$(LD) $(LDFLAGS) $(LIBPATH) -o $@ egl.def $(EGL_OBJS) $(LIBS)
#	$(STRIP) $@
#	sed -e "s/ @[^ ]*//" egl1.def > egl.def
	mv -f $@ ../../../bin
	mv -f lib$(EGL).dll.a ../../../lib   

libglsl.a : $(GLSL_OBJS) Makefile
	$(AR) libglsl.a $(GLSL_OBJS) 
	mv -f libglsl.a ../../../lib

libGL.dll: $(LIBGL_OBJS) Makefile
	$(LD) $(LDFLAGS) $(LIBPATH) -o $@ $(LIBGL_OBJS) $(LIBS)
#	$(STRIP) $@
#	sed -e "s/ @[^ ]*//" egl1.def > egl.def
	mv -f $@ ../../bin
	mv -f libGL.dll.a ../../../lib   

src/glsl/glcpp/%.o : src/glsl/glcpp/%.c Makefile
	$(CC) $(CFLAGS) -std=c99 $(MESA_DEFS) $(INC_MESA) -o $@ $<

src/glsl/%.o : src/glsl/%.c Makefile
	$(CC) $(CFLAGS) -std=c99 $(MESA_DEFS) $(INC_MESA) -o $@ $<

src/mapi/glapi/%.o : src/mapi/glapi/%.c Makefile
	$(CC) $(CFLAGS) -std=c99 $(MESA_DEFS) $(INC_MESA) -o $@ $<
	
%.o : %.c Makefile
	$(CC) $(CFLAGS) $(MESA_DEFS) $(EGL_DEFS) $(INC_EGL) -o $@ $<


%.o : %.cpp Makefile
	$(CC) $(CFLAGS) $(MESA_DEFS) $(INC_MESA) -o $@ $<
 