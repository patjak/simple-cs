/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Simple program to run a compute shader in a headless EGL/GLESv31 context.
 *
 * Author: Patrik Jakobsson <pjakobsson@suse.de>
 * Copyright (C) 2020 SUSE LLC
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <gbm.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int verbose = 0;

struct context {
	EGLint egl_major, egl_minor;
	struct gbm_device *gbm;
	EGLDisplay egl_disp;
	EGLContext egl_ctx;
	int node_fd;
};

struct shader {
	GLuint program_id;
};

void context_init(struct context *ctx)
{
	const char *egl_exts;
	EGLConfig config;
	EGLBoolean ret;
	EGLint count;

	/* Open DRM render node */
	ctx->node_fd = open("/dev/dri/renderD128", O_RDWR);
	assert(ctx->node_fd > 0);

	/* Create GBM device */
	ctx->gbm = gbm_create_device(ctx->node_fd);
	assert(ctx->gbm > 0);

	/* Get EGL platform display */
	ctx->egl_disp = eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA, ctx->gbm, NULL);
	assert(ctx->egl_disp != EGL_NO_DISPLAY);

	/* Initialize EGL */
	ret = eglInitialize(ctx->egl_disp, &ctx->egl_major, &ctx->egl_minor);
	assert(ret);

	/* Check for required extensions */
	egl_exts = eglQueryString(ctx->egl_disp, EGL_EXTENSIONS);
	assert(strstr(egl_exts, "EGL_KHR_create_context") != NULL);
	assert(strstr(egl_exts, "EGL_KHR_surfaceless_context") != NULL);

	/* Choose EGL config */
	static const EGLint attr_list[] = {
		EGL_RENDERABLE_TYPE,
		EGL_OPENGL_ES3_BIT_KHR,
		EGL_NONE
	};
	ret = eglChooseConfig (ctx->egl_disp, attr_list, &config, 1, &count);
	assert(ret);

	/* Bind GLES API */
	ret = eglBindAPI (EGL_OPENGL_ES_API);
	assert(ret);

	/* Create EGL context */
	static const EGLint ctx_attr[] = {
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_NONE
	};
	ctx->egl_ctx = eglCreateContext(ctx->egl_disp,
					config,
					EGL_NO_CONTEXT,
					ctx_attr);
	assert(ctx->egl_ctx != EGL_NO_CONTEXT);

	/* Set EGL context to current */
	ret = eglMakeCurrent(ctx->egl_disp, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx->egl_ctx);
	assert(ret);
}

void context_print_info(struct context *ctx)
{
	const char *egl_exts;
	const GLubyte *str;

	printf("EGL v%d.%d\n", ctx->egl_major, ctx->egl_minor);

	if (verbose) {
		egl_exts = eglQueryString(ctx->egl_disp, EGL_EXTENSIONS);
		printf("EGL Extensions: %s\n", egl_exts);
	}

	str = glGetString(GL_VENDOR);
	printf("GL Vendor: %s\n", str);

	str = glGetString(GL_RENDERER);
	printf("GL Renderer: %s\n", str);

	str = glGetString(GL_VERSION);
	printf("GL Version: %s\n", str);

	str = glGetString(GL_SHADING_LANGUAGE_VERSION);
	printf("GL Shading language: %s\n", str);

	if (verbose) {
		str = glGetString(GL_EXTENSIONS);
		printf("GL Extensions: %s\n", str);
	}
}

void context_uninit(struct context *ctx)
{
	eglDestroyContext(ctx->egl_disp, ctx->egl_ctx);
	ctx->egl_disp = ctx->egl_ctx = NULL;

	eglTerminate(ctx->egl_disp);
	ctx->egl_disp = NULL;

	gbm_device_destroy(ctx->gbm);
	ctx->gbm = NULL;
}

void shader_load(struct shader *shader, const char *filename)
{
	GLuint shader_id;
	GLchar *log_buf;
	GLint success;
	GLint log_size;
	long filesize;
	char *src;
	FILE *f;

	f = fopen(filename, "rb");
	assert(f != NULL);
	fseek(f, 0, SEEK_END);
	filesize = ftell(f);
	rewind(f);

	src = malloc(filesize + 1);
	fread(src, 1, filesize, f);
	fclose(f);

	src[filesize] = '\0';
	if (verbose)
		printf("Compute shader source:\n%s\n", src);

	shader_id = glCreateShader(GL_COMPUTE_SHADER);
	assert(glGetError() == GL_NO_ERROR);

	glShaderSource(shader_id, 1, (const GLchar **)&src, NULL);
	assert(glGetError() == GL_NO_ERROR);

	glCompileShader(shader_id);
	assert(glGetError() == GL_NO_ERROR);

	/* Check for compilation errors */
	glGetShaderiv(shader_id, GL_COMPILE_STATUS, &success);
	if (success == GL_FALSE) {
		glGetShaderiv(shader_id, GL_INFO_LOG_LENGTH, &log_size);
		log_buf = (char *)malloc(log_size);
		glGetShaderInfoLog(shader_id, log_size, &log_size, log_buf);
		printf("Shader compilation error: %s\n %s\n", filename, log_buf);
		free(log_buf);
		assert(success != GL_FALSE);
        }

	shader->program_id = glCreateProgram();

	glAttachShader(shader->program_id, shader_id);
	assert(glGetError() == GL_NO_ERROR);

	glLinkProgram(shader->program_id);
	assert(glGetError() == GL_NO_ERROR);

	glDeleteShader(shader_id);
}

void shader_run(struct shader *shader)
{
	glUseProgram(shader->program_id);
	assert(glGetError() == GL_NO_ERROR);

	glDispatchCompute(1, 1, 1);
	assert(glGetError() == GL_NO_ERROR);
}

void shader_unload(struct shader *shader)
{
	glDeleteProgram(shader->program_id);
}

int main(int argc, char **argp)
{
	struct context ctx;
	struct shader shader;

	context_init(&ctx);
	context_print_info(&ctx);

	shader_load(&shader, "shader.cs");
	shader_run(&shader);
	shader_unload(&shader);

	context_uninit(&ctx);

	return 0;
}
