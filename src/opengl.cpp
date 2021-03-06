#include "opengl.hpp"
#include <compositor.h>
#include "debug.hpp"
#include "output.hpp"
#include "render-manager.hpp"
#include <gl-renderer-api.h>

namespace {
    OpenGL::context_t *bound;
}

const char* gl_error_string(const GLenum err) {
    switch (err) {
        case GL_INVALID_ENUM:
            return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE:
            return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION:
            return "GL_INVALID_OPERATION";
        case GL_OUT_OF_MEMORY:
            return "GL_OUT_OF_MEMORY";
    }
    return "UNKNOWN GL ERROR";
}

void gl_call(const char *func, uint32_t line, const char *glfunc) {
    GLenum err;
    if ((err = glGetError()) == GL_NO_ERROR)
        return;

    debug << "gles: function " << func << " at line " << line << ": \n" << glfunc << " == " << gl_error_string(err) << "\n";
}

namespace OpenGL {
    GLuint compile_shader(const char *src, GLuint type)
    {
        GLuint shader = GL_CALL(glCreateShader(type));
        GL_CALL(glShaderSource(shader, 1, &src, NULL));

        int s;
        char b1[10000];
        GL_CALL(glCompileShader(shader));
        GL_CALL(glGetShaderiv(shader, GL_COMPILE_STATUS, &s));
        GL_CALL(glGetShaderInfoLog(shader, 10000, NULL, b1));

        if ( s == GL_FALSE )
        {

            errio << "shader compilation failed!\n"
                    "src: ***************************\n" <<
                    src <<
                    "********************************\n" <<
                    b1 <<
                    "********************************\n";
            return -1;
        }
        return shader;
    }

    GLuint load_shader(const char *path, GLuint type) {

        std::fstream file(path, std::ios::in);
        if(!file.is_open())
        {
            errio << "Cannot open shader file " << path << "." << std::endl;
            return -1;
        }

        std::string str, line;

        while(std::getline(file, line))
            str += line, str += '\n';

        auto sh = compile_shader(str.c_str(), type);
        if (sh == (uint)-1)
            errio << "Cannot open shader file " << path << "." << std::endl;

        return sh;
    }

    /*
    const char *getStrSrc(GLenum src) {
        if(src == GL_DEBUG_SOURCE_API            )return "API";
        if(src == GL_DEBUG_SOURCE_WINDOW_SYSTEM  )return "WINDOW_SYSTEM";
        if(src == GL_DEBUG_SOURCE_SHADER_COMPILER)return "SHADER_COMPILER";
        if(src == GL_DEBUG_SOURCE_THIRD_PARTY    )return "THIRD_PARTYB";
        if(src == GL_DEBUG_SOURCE_APPLICATION    )return "APPLICATIONB";
        if(src == GL_DEBUG_SOURCE_OTHER          )return "OTHER";
        else return "UNKNOWN";
    }

    const char *getStrType(GLenum type) {
        if(type==GL_DEBUG_TYPE_ERROR              )return "ERROR";
        if(type==GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR)return "DEPRECATED_BEHAVIOR";
        if(type==GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR )return "UNDEFINED_BEHAVIOR";
        if(type==GL_DEBUG_TYPE_PORTABILITY        )return "PORTABILITY";
        if(type==GL_DEBUG_TYPE_PERFORMANCE        )return "PERFORMANCE";
        if(type==GL_DEBUG_TYPE_OTHER              )return "OTHER";
        return "UNKNOWN";
    }

    const char *getStrSeverity(GLenum severity) {
        if(severity == GL_DEBUG_SEVERITY_HIGH  )return "HIGH";
        if(severity == GL_DEBUG_SEVERITY_MEDIUM)return "MEDIUM";
        if(severity == GL_DEBUG_SEVERITY_LOW   )return "LOW";
        if(severity == GL_DEBUG_SEVERITY_NOTIFICATION) return "NOTIFICATION";
        return "UNKNOWN";
    }

    void errorHandler(GLenum src, GLenum type,
            GLuint id, GLenum severity,
            GLsizei len, const GLchar *msg,
            const void *dummy) {
        // ignore notifications
        if(severity == GL_DEBUG_SEVERITY_NOTIFICATION)
            return;
        debug << "_______________________________________________\n";
        debug << "GLES debug: \n";
        debug << "Source: " << getStrSrc(src) << std::endl;
        debug << "Type: " << getStrType(type) << std::endl;
        debug << "ID: " << id << std::endl;
        debug << "Severity: " << getStrSeverity(severity) << std::endl;
        debug << "Msg: " << msg << std::endl;;
        debug << "_______________________________________________\n";
    } */

#define load_program(suffix) \
    GLuint fss_ ## suffix = load_shader(std::string(shaderSrcPath) \
                                      .append("/frag_" #suffix ".glsl").c_str(), \
                                      GL_FRAGMENT_SHADER); \
    \
    ctx->program_ ## suffix = GL_CALL(glCreateProgram());\
    GL_CALL(glAttachShader(ctx->program_ ## suffix, vss));\
    GL_CALL(glAttachShader(ctx->program_ ## suffix, fss_ ## suffix));\
    GL_CALL(glLinkProgram(ctx->program_ ## suffix)); \
    GL_CALL(glUseProgram(ctx->program_ ## suffix))

    context_t* create_gles_context(wayfire_output *output, const char *shaderSrcPath)
    {
        context_t *ctx = new context_t;
        ctx->output = output;

        /*
        if (file_debug == &file_info) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(errorHandler, 0);
        } */

        GLuint vss = load_shader(std::string(shaderSrcPath)
                    .append("/vertex.glsl").c_str(),
                     GL_VERTEX_SHADER);

        load_program(rgba);
        load_program(rgbx);
        load_program(egl);
        load_program(y_uv);
        load_program(y_u_v);
        load_program(y_xuxv);

#undef load_program

        ctx->mvpID   = GL_CALL(glGetUniformLocation(ctx->program_rgba, "MVP"));
        ctx->colorID = GL_CALL(glGetUniformLocation(ctx->program_rgba, "color"));

        ctx->w2ID = GL_CALL(glGetUniformLocation(ctx->program_rgba, "w2"));
        ctx->h2ID = GL_CALL(glGetUniformLocation(ctx->program_rgba, "h2"));

        ctx->position   = GL_CALL(glGetAttribLocation(ctx->program_rgba, "position"));
        ctx->uvPosition = GL_CALL(glGetAttribLocation(ctx->program_rgba, "uvPosition"));
        return ctx;
    }

    void use_default_program(uint32_t bits)
    {
        GLuint program = bound->program_rgba;
        if (bits & TEXTURE_RGBX)
            program = bound->program_rgbx;
        if (bits & TEXTURE_EGL)
            program = bound->program_egl;
        if (bits & TEXTURE_Y_UV)
            program = bound->program_y_uv;
        if (bits & TEXTURE_Y_U_V)
            program = bound->program_y_u_v;
        if (bits & TEXTURE_Y_XUXV)
            program = bound->program_y_xuxv;

        GL_CALL(glUseProgram(program));
    }

    void bind_context(context_t *ctx) {
        bound = ctx;

        bound->width  = ctx->output->handle->width;
        bound->height = ctx->output->handle->height;
    }

    weston_geometry get_device_viewport()
    {
        return render_manager::renderer_api->get_output_gl_viewport(bound->output->handle);
    }

    void use_device_viewport()
    {
        const auto vp = get_device_viewport();
        GL_CALL(glViewport(vp.x, vp.y, vp.width, vp.height));
    }

    void release_context(context_t *ctx) {
	    glDeleteProgram(ctx->program_rgba);
	    glDeleteProgram(ctx->program_rgbx);
	    delete ctx;
    }

    void render_texture(GLuint tex[], int n_tex, GLenum target,
                        const weston_geometry& g,
                        const texture_geometry& texg, uint32_t bits)
    {
        if ((bits & DONT_RELOAD_PROGRAM) == 0)
            use_default_program(bits);

        GL_CALL(glUniform1f(bound->w2ID, bound->width / 2));
        GL_CALL(glUniform1f(bound->h2ID, bound->height / 2));

        if ((bits & TEXTURE_TRANSFORM_USE_DEVCOORD))
        {
            use_device_viewport();
        } else
        {
            GL_CALL(glViewport(0, 0, bound->width, bound->height));
        }

        float w2 = float(bound->width) / 2.;
        float h2 = float(bound->height) / 2.;

        float tlx = float(g.x) - w2,
              tly = h2 - float(g.y);

        float w = g.width;
        float h = g.height;

        if(bits & TEXTURE_TRANSFORM_INVERT_Y) {
            h   *= -1;
            tly += h;
        }

        GLfloat vertexData[] = {
            tlx    , tly - h, 0.f, // 1
            tlx + w, tly - h, 0.f, // 2
            tlx + w, tly    , 0.f, // 3
            tlx    , tly    , 0.f, // 4
        };

        GLfloat coordData[] = {
            0.0f, 1.0f,
            1.0f, 1.0f,
            1.0f, 0.0f,
            0.0f, 0.0f,
        };

        if (bits & TEXTURE_USE_TEX_GEOMETRY) {
            coordData[0] = texg.x1; coordData[1] = texg.y2;
            coordData[2] = texg.x2; coordData[3] = texg.y2;
            coordData[4] = texg.x2; coordData[5] = texg.y1;
            coordData[6] = texg.x1; coordData[7] = texg.y1;
        }

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

        for (int i = 0; i < n_tex; i++)
        {
            GL_CALL(glBindTexture(target, tex[i]));
            GL_CALL(glActiveTexture(GL_TEXTURE0 + i));
            GL_CALL(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            GL_CALL(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        }

        GL_CALL(glVertexAttribPointer(bound->position, 3, GL_FLOAT, GL_FALSE, 0, vertexData));
        GL_CALL(glEnableVertexAttribArray(bound->position));

        GL_CALL(glVertexAttribPointer(bound->uvPosition, 2, GL_FLOAT, GL_FALSE, 0, coordData));
        GL_CALL(glEnableVertexAttribArray(bound->uvPosition));

        GL_CALL(glDrawArrays (GL_TRIANGLE_FAN, 0, 4));

        GL_CALL(glDisableVertexAttribArray(bound->position));
        GL_CALL(glDisableVertexAttribArray(bound->uvPosition));
    }

    void render_texture(GLuint tex, const weston_geometry& g,
                        const texture_geometry& texg, uint32_t bits)
    { render_texture(&tex, 1, GL_TEXTURE_2D, g, texg, bits); }


    void render_transformed_texture(GLuint tex[], int n_tex, GLenum target,
                                    const weston_geometry& g,
                                    const texture_geometry& texg,
                                    glm::mat4 transform, glm::vec4 color, uint32_t bits)
    {
        use_default_program(bits);
        GL_CALL(glUniformMatrix4fv(bound->mvpID, 1, GL_FALSE, &transform[0][0]));
        GL_CALL(glUniform4fv(bound->colorID, 1, &color[0]));

        GL_CALL(glEnable(GL_BLEND));
        GL_CALL(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        render_texture(tex, n_tex, target,
                       g, texg, bits | DONT_RELOAD_PROGRAM);
        GL_CALL(glDisable(GL_BLEND));
    }

    void render_transformed_texture(GLuint text, const weston_geometry& g,
                                    const texture_geometry& texg,
                                    glm::mat4 transform, glm::vec4 color, uint32_t bits)
    {
        render_transformed_texture(&text, 1, GL_TEXTURE_2D, g, texg,
                                   transform, color, bits);
    }


    void prepare_framebuffer(GLuint &fbuff, GLuint &texture,
                             float scale_x, float scale_y)
    {
        if (fbuff == (uint)-1)
            GL_CALL(glGenFramebuffers(1, &fbuff));
        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, fbuff));

        bool existing_texture = (texture != (uint)-1);
        if (!existing_texture)
            GL_CALL(glGenTextures(1, &texture));

        GL_CALL(glBindTexture(GL_TEXTURE_2D, texture));

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT));

        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));

        if (!existing_texture)
            GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                        bound->width * scale_x, bound->height * scale_y,
                        0, GL_RGBA, GL_UNSIGNED_BYTE, 0));

        GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                texture, 0));

        auto status = GL_CALL(glCheckFramebufferStatus(GL_FRAMEBUFFER));
        if (status != GL_FRAMEBUFFER_COMPLETE)
            errio << "Error in framebuffer!\n";
    }

    GLuint duplicate_texture(GLuint tex, int w, int h)
    {
        GLuint dst_tex = -1;

        GLuint dst_fbuff = -1, src_fbuff = -1;

        prepare_framebuffer(dst_fbuff, dst_tex);
        GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h,
                             0, GL_RGBA, GL_UNSIGNED_BYTE, 0));

        prepare_framebuffer(src_fbuff, tex);

        GL_CALL(glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbuff));
        GL_CALL(glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR));

        GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

        GL_CALL(glDeleteFramebuffers(1, &dst_fbuff));
        GL_CALL(glDeleteFramebuffers(1, &src_fbuff));
        return dst_tex;
    }
}
