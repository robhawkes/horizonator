#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <GL/glew.h>
#include <GL/freeglut.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <opencv2/highgui/highgui_c.h>

#include "dem_downloader.h"

// can be used for testing/debugging to turn off the seam rendering
#define NOSEAM 0


static enum { PM_FILL, PM_LINE, PM_POINT, PM_NUM } PolygonMode = PM_FILL;
static int Ntriangles, Nvertices;

static GLint uniform_aspect;


// These are such because of the layout of the SRTM DEMs
#define WDEM        1201
#define gridW       1200
#define gridH       1200

#define FOVY_DEG    30.0 /* vertical field of view of the render */

#define OFFSCREEN_W 2000.0
#define OFFSCREEN_H (int)( 0.5 + OFFSCREEN_W / 360.0 * FOVY_DEG)

static bool loadGeometry( float view_lat, float view_lon, float* viewer_z )
{
  unsigned char* dem;

  GLint          uniform_view_z;
  GLint          uniform_demfileN, uniform_demfileW;
  GLint          uniform_WDEM;
  GLint          uniform_view_lon, uniform_view_lat;
  GLint          uniform_sin_view_lat, uniform_cos_view_lat;


  int16_t sampleDEM(int i, int j)
  {
    uint32_t p = i + j*WDEM;
    int16_t  z = (int16_t) ((dem[2*p] << 8) | dem[2*p + 1]);
    return (z < 0) ? 0 : z;
  }

  float getHeight(int i, int j)
  {
    // return the largest height in the 4 neighboring cells
    bool inrange(int i, int j)
    {
      return
        i >= 0 && i < WDEM &&
        j >= 0 && j < WDEM;
    }

    float z = -1e20f;
    if( inrange(i,  j  ) ) z = fmaxf(z, (float) sampleDEM(i,  j  ) );
    if( inrange(i+1,j  ) ) z = fmaxf(z, (float) sampleDEM(i+1,j  ) );
    if( inrange(i,  j+1) ) z = fmaxf(z, (float) sampleDEM(i,  j+1) );
    if( inrange(i+1,j+1) ) z = fmaxf(z, (float) sampleDEM(i+1,j+1) );

    return z;
  }





  // Viewer is looking north, the seam is behind (to the south). If the viewer is
  // directly on a grid value, then the cell of the seam is poorly defined. In
  // that scenario, I nudge the viewer to one side to unambiguously pick the seam
  // cell
  float cell_idx         = view_lon * WDEM;
  float cell_idx_rounded = floorf( cell_idx + 0.5f );
  float diff = fabsf( cell_idx - cell_idx_rounded );

  // want at least 0.1 cells of separation
  if( diff < 0.1 * 2.0 )
    view_lon -= 0.1/WDEM;

  int demfileN =  (int)floorf( view_lat );
  int demfileW = -(int)floorf( view_lon );


  // grid starts at the NW corner, and traverses along the latitude first.
  // DEM tile is named from the SW point
  float lat_from_idx(int j)
  {
    return  (float)demfileN + 1.0f - (float)j/(float)(WDEM-1);
  }

  float lon_from_idx(int i)
  {
    return -(float)demfileW        + (float)i/(float)(WDEM-1);
  }

  int floor_idx_from_lat(float lat)
  {
    return floorf( ((float)demfileN + 1.0f - lat) * (float)(WDEM-1) );
  }

  int floor_idx_from_lon(float lon)
  {
    return floorf( ((float)demfileW        + lon) * (float)(WDEM-1) );
  }





  // This function will try to download the DEM if it's not found
  const char* filename = getDEM_filename( demfileN, demfileW );
  if( filename == NULL )
    return false;

  struct stat sb;
  int fd = open( filename, O_RDONLY );
  assert(fd > 0);
  assert( fstat(fd, &sb) == 0 );
  dem = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if( dem == MAP_FAILED )
    return false;

  if( WDEM*WDEM*2 != sb.st_size )
  {
    munmap( dem, sb.st_size );
    return false;
  }



  Nvertices = (gridW + 1) * (gridH + 1);
  Ntriangles = gridW*gridH*2;

  // seam business
  int Lseam = 0;
  int view_i, view_j;
  {
    // if we're doing a mercator projection, we must take care of the seam. The
    // camera always looks north, so the seam is behind us. Behind me are two
    // rows of vertices, one on either side. With a mercator projection, these
    // rows actually appear on opposite ends of the resulting image, and thus I
    // do not want to simply add triangles into this gap. Instead, I double-up
    // each of these rows, place the duplicated vertices off screen (angle < -pi
    // for one row and angle > pi for the other), and render the seam twice,
    // once for each side.
    //
    // Furthermore, I do not render the two triangles that span the cell that
    // the camera is in
    view_i = floor_idx_from_lon(view_lon);
    view_j = floor_idx_from_lat(view_lat);

    *viewer_z = getHeight(view_i, view_j);

    Lseam = gridH - view_j;

#if NOSEAM == 0
    Nvertices  += Lseam*2;      // double-up the seam vertices
    Ntriangles += (Lseam-1)*2;  // Seam rendered twice. This is the extra one
#else
    Ntriangles -= (Lseam-1)*2;
#endif

    Ntriangles -= 2;            // Don't render the triangles AT the viewer
  }

  // vertices
  //
  // I fill in the VBO. Each point is a 16-bit integer tuple (ilon,ilat,height).
  // The first 2 args are indices into the DEM. The height is in meters
  {
    GLuint vertexBufID;
    glGenBuffers(1, &vertexBufID);
    glBindBuffer(GL_ARRAY_BUFFER, vertexBufID);
    glBufferData(GL_ARRAY_BUFFER, Nvertices*3*sizeof(GLshort), NULL, GL_STATIC_DRAW);
    glVertexPointer(3, GL_SHORT, 0, NULL);

    GLshort* vertices = glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
    int idx = 0;
    for( int j=0; j<=gridH; j++ )
    {
      for( int i=0; i<=gridW; i++ )
      {
        vertices[idx++] = i;
        vertices[idx++] = j;
        vertices[idx++] = sampleDEM(i,j);
      }
    }

#if NOSEAM == 0
    // add the extra seam vertices
    if( Lseam )
    {
      for( int j=view_j+1; j<=gridH; j++ )
      {
        // These duplicates have the same geometry as the originals, but the
        // shader will project them differently, by moving the resulting angle
        // by 2*pi

        // left side
        vertices[idx++] = view_i;
        vertices[idx++] = j - 2*WDEM; // negative to indicate that this is a duplicate for the left seam
        vertices[idx++] = sampleDEM(view_i,j);

        // right side
        vertices[idx++] = view_i+1 - 2*WDEM; // negative to indicate that this is a duplicate for the right seam
        vertices[idx++] = j;
        vertices[idx++] = sampleDEM(view_i+1,j);
      }
    }
#endif

    assert( glUnmapBuffer(GL_ARRAY_BUFFER) == GL_TRUE );
    assert( idx == Nvertices*3 );
  }
  close(fd);

  // indices
  {
    GLuint indexBufID;
    glGenBuffers(1, &indexBufID);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBufID);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, Ntriangles*3*sizeof(GLuint), NULL, GL_STATIC_DRAW);

    GLuint* indices = glMapBuffer(GL_ELEMENT_ARRAY_BUFFER, GL_WRITE_ONLY);
    int idx = 0;
    for( int j=0; j<gridH; j++ )
    {
      for( int i=0; i<gridW; i++ )
      {
        // seam?
        if( i == view_i)
        {
          // do not render the triangles the camera is sitting on
          if( j == view_j )
            continue;

          if( j >= view_j+1 )
          {
#if NOSEAM == 0
            // seam. I add two sets of triangles here; one for the left edge of
            // the screen and one for the right
            int jseam = j - (view_j + 1);

            // left edge:
            indices[idx++] = (gridH+1)*(gridW+1) +  jseam     *2;
            indices[idx++] = (gridH+1)*(gridW+1) + (jseam + 1)*2;
            indices[idx++] = (j + 1)*(gridW+1) + (i + 1);

            indices[idx++] = (gridH+1)*(gridW+1) +  jseam     *2;
            indices[idx++] = (j + 1)*(gridW+1) + (i + 1);
            indices[idx++] = (j + 0)*(gridW+1) + (i + 1);

            // right edge:
            indices[idx++] = (j + 0)*(gridW+1) + (i + 0);
            indices[idx++] = (j + 1)*(gridW+1) + (i + 0);
            indices[idx++] = (gridH+1)*(gridW+1) + (jseam + 1)*2 + 1;

            indices[idx++] = (j + 0)*(gridW+1) + (i + 0);
            indices[idx++] = (gridH+1)*(gridW+1) + (jseam + 1)*2 + 1;
            indices[idx++] = (gridH+1)*(gridW+1) +  jseam     *2 + 1;
#endif

            continue;
          }
        }

        // non-seam
        indices[idx++] = (j + 0)*(gridW+1) + (i + 0);
        indices[idx++] = (j + 1)*(gridW+1) + (i + 0);
        indices[idx++] = (j + 1)*(gridW+1) + (i + 1);

        indices[idx++] = (j + 0)*(gridW+1) + (i + 0);
        indices[idx++] = (j + 1)*(gridW+1) + (i + 1);
        indices[idx++] = (j + 0)*(gridW+1) + (i + 1);
      }
    }
    assert( glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER) == GL_TRUE );
    assert(idx == Ntriangles*3);
  }

  // shaders
  {
    // The shader transforms the VBO vertices into the view coord system. Each VBO
    // point is a 16-bit integer tuple (ilon,ilat,height). The first 2 args are
    // indices into the DEM. The height is in meters
    const GLchar* vertexShaderSource =
#include "vertex.glsl.h"
      ;

    const GLchar* fragmentShaderSource =
#include "fragment.glsl.h"
      ;

    char msg[1024];
    int len;
    GLuint program =glCreateProgram();
    assert( glGetError() == GL_NO_ERROR );


#define installshader(type,TYPE)                                        \
    GLuint type ## Shader = glCreateShader(GL_ ## TYPE ## _SHADER);     \
    assert( glGetError() == GL_NO_ERROR );                              \
                                                                        \
    glShaderSource(type ## Shader, 1, (const GLchar**)&type ## ShaderSource, NULL); \
    assert( glGetError() == GL_NO_ERROR );                              \
                                                                        \
    glCompileShader(type ## Shader);                                    \
    assert( glGetError() == GL_NO_ERROR );                              \
    glGetShaderInfoLog( type ## Shader, sizeof(msg), &len, msg );       \
    if( strlen(msg) )                                                   \
      printf(#type " msg: %s\n", msg);                                  \
                                                                        \
    glAttachShader(program, type ##Shader);                             \
    assert( glGetError() == GL_NO_ERROR );



    installshader(vertex, VERTEX);
    installshader(fragment, FRAGMENT);

    glLinkProgram(program); assert( glGetError() == GL_NO_ERROR );
    glUseProgram(program);  assert( glGetError() == GL_NO_ERROR );


    uniform_view_z       = glGetUniformLocation(program, "view_z"      ); assert( glGetError() == GL_NO_ERROR );
    uniform_demfileN     = glGetUniformLocation(program, "demfileN"    ); assert( glGetError() == GL_NO_ERROR );
    uniform_demfileW     = glGetUniformLocation(program, "demfileW"    ); assert( glGetError() == GL_NO_ERROR );
    uniform_WDEM         = glGetUniformLocation(program, "WDEM"        ); assert( glGetError() == GL_NO_ERROR );
    uniform_view_lat     = glGetUniformLocation(program, "view_lat"    ); assert( glGetError() == GL_NO_ERROR );
    uniform_view_lon     = glGetUniformLocation(program, "view_lon"    ); assert( glGetError() == GL_NO_ERROR );
    uniform_sin_view_lat = glGetUniformLocation(program, "sin_view_lat"); assert( glGetError() == GL_NO_ERROR );
    uniform_cos_view_lat = glGetUniformLocation(program, "cos_view_lat"); assert( glGetError() == GL_NO_ERROR );
    uniform_aspect       = glGetUniformLocation(program, "aspect"      ); assert( glGetError() == GL_NO_ERROR );

    glUniform1f( uniform_view_z,       *viewer_z);
    glUniform1i( uniform_demfileN,     demfileN);
    glUniform1i( uniform_demfileW,     demfileW);
    glUniform1i( uniform_WDEM,         WDEM);

    glUniform1f( uniform_view_lon,     view_lon * M_PI / 180.0f );
    glUniform1f( uniform_view_lat,     view_lat * M_PI / 180.0f );
    glUniform1f( uniform_sin_view_lat, sinf( M_PI / 180.0f * view_lat ));
    glUniform1f( uniform_cos_view_lat, cosf( M_PI / 180.0f * view_lat ));
  }

  munmap( dem, sb.st_size );

  return true;
}


static void window_reshape(int width, int height)
{
  glViewport(0, 0, width, height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glUniform1f(uniform_aspect, (float)width / (float)height);
}

static void do_draw(void)
{
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  static const GLenum pmMap[] = {GL_FILL, GL_LINE, GL_POINT};
  glPolygonMode(GL_FRONT_AND_BACK, pmMap[ PolygonMode ] );

  glEnable(GL_CULL_FACE);

  // draw
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_INDEX_ARRAY);
  glDrawElements(GL_TRIANGLES, Ntriangles*3, GL_UNSIGNED_INT, NULL);


  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDisable(GL_CULL_FACE);
}

static void window_display(void)
{
  do_draw();
  glutSwapBuffers();
}

static void window_keyPressed(unsigned char key, int x, int y)
{
  static GLenum winding = GL_CCW;

  switch (key)
  {
  case 'w':
    if(++PolygonMode == PM_NUM)
      PolygonMode = 0;
    break;

  case 'r':
    if (winding == GL_CCW) winding = GL_CW;
    else                   winding = GL_CCW;
    glFrontFace(winding);
    break;

  case 'q':
  case 27:
    exit(0);
  }

  glutPostRedisplay();
}


static IplImage* readOffscreenPixels( bool do_bgr )
{
  CvSize size = { .width  = OFFSCREEN_W,
                  .height = OFFSCREEN_H };

  IplImage* img = cvCreateImage(size, 8, 3);
  assert( img );

  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  glReadPixels(0,0, OFFSCREEN_W, OFFSCREEN_H,
               do_bgr ? GL_BGR : GL_RGB,
               GL_UNSIGNED_BYTE, img->imageData);
  cvFlip(img, NULL, 0);
  return img;
}

static bool setup_gl( bool doRenderToScreen,
                      float view_lat, float view_lon,
                      float* viewer_z )
{
  void DoFeatureChecks(void)
  {
    char *version = (char *) glGetString(GL_VERSION);
    if (version[0] == '1') {
      /* check for individual extensions */
      if (!glutExtensionSupported("GL_ARB_vertex_shader")) {
        printf("Sorry, GL_ARB_vertex_shader is required.\n");
        exit(1);
      }
      if (!glutExtensionSupported("GL_ARB_fragment_shader")) {
        printf("Sorry, GL_ARB_fragment_shader is required.\n");
        exit(1);
      }
      if (!glutExtensionSupported("GL_ARB_vertex_buffer_object")) {
        printf("Sorry, GL_ARB_vertex_buffer_object is required.\n");
        exit(1);
      }
      if (!glutExtensionSupported("GL_EXT_framebuffer_object")) {
        printf("GL_EXT_framebuffer_object not found!\n");
        exit(1);
      }
    }
  }

  void createOffscreenTargets(void)
  {
    GLuint frameBufID;
    {
      glGenFramebuffers(1, &frameBufID);
      assert( glGetError() == GL_NO_ERROR );

      glBindFramebuffer(GL_FRAMEBUFFER, frameBufID);
      assert( glGetError() == GL_NO_ERROR );
    }

    {
      GLuint renderBufID;
      glGenRenderbuffers(1, &renderBufID);
      assert( glGetError() == GL_NO_ERROR );

      glBindRenderbuffer(GL_RENDERBUFFER, renderBufID);
      assert( glGetError() == GL_NO_ERROR );

      glRenderbufferStorage(GL_RENDERBUFFER, GL_RGB, OFFSCREEN_W, OFFSCREEN_H);
      assert( glGetError() == GL_NO_ERROR );

      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_RENDERBUFFER, renderBufID);
      assert( glGetError() == GL_NO_ERROR );
    }

    {
      GLuint depthBufID;
      glGenRenderbuffers(1, &depthBufID);
      assert( glGetError() == GL_NO_ERROR );

      glBindRenderbuffer(GL_RENDERBUFFER, depthBufID);
      assert( glGetError() == GL_NO_ERROR );

      glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, OFFSCREEN_W, OFFSCREEN_H);
      assert( glGetError() == GL_NO_ERROR );

      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                GL_RENDERBUFFER, depthBufID);
      assert( glGetError() == GL_NO_ERROR );
    }

    assert( glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE );
  }



  static bool already_setup = false;
  if( already_setup )
    return false;
  already_setup = true;

  glutInit(&(int){1}, &(char*){"exec"});
  glutInitDisplayMode( GLUT_RGB | GLUT_DEPTH | ( doRenderToScreen ? GLUT_DOUBLE : 0 ));

  // when offscreen, I really don't want to glutCreateWindow(), but for some
  // reason not doing this causes glewInit() to segfault...
  glutCreateWindow("Terrain renderer");
  glewInit();
  DoFeatureChecks();

  if( doRenderToScreen )
  {
    glutKeyboardFunc(window_keyPressed);
    glutReshapeFunc (window_reshape);
    glutDisplayFunc (window_display);
  }
  else
    createOffscreenTargets();

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glEnable(GL_NORMALIZE);
  glClearColor(0, 0, 1, 0);

  return loadGeometry( view_lat, view_lon, viewer_z );
}

// returns the rendered opencv image. NULL on error. It is the caller's
// responsibility to free this image's memory
IplImage* render_terrain( float view_lat, float view_lon, float* elevation,
                          bool do_bgr )
{
  static float viewer_z = -1.0;
  float viewer_z_new;
  if( !setup_gl( false, view_lat, view_lon, &viewer_z_new ) )
    return NULL;


  viewer_z = *elevation = viewer_z_new;

  window_reshape(OFFSCREEN_W, OFFSCREEN_H);
  do_draw();

  IplImage* img = readOffscreenPixels( do_bgr );
  glutExit();
  return img;
}

bool render_terrain_to_window( float view_lat, float view_lon )
{
  float viewer_z_new;
  if( setup_gl( true, view_lat, view_lon, &viewer_z_new ) )
  {
    glutMainLoop();
    return true;
  }
  return false;
}
