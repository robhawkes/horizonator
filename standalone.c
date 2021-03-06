#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <FreeImage.h>
#include <math.h>

#include "horizonator.h"
#include "util.h"

int main(int argc, char* argv[])
{
    const char* usage =
        "%s [--width WIDTH_PIXELS] [--height HEIGHT_PIXELS]\n"
        "   [--image OUT.png] [--ranges RANGES.DAT]\n"
        "   [--radius RENDER_RADIUS_CELLS]\n"
        "   [--texture]\n"
        "   [--allow-tile-downloads]\n"
        "   [--znear       ZNEAR]\n"
        "   [--zfar        ZFAR]\n"
        "   [--znear-color ZNEARCOLOR]\n"
        "   [--zfar-color  ZFARCOLOR]\n"
        "   [--dirdems DIRECTORY]\n"
        "   [--dirtiles DIRECTORY]\n"
        "   LAT LON AZ_DEG0 AZ_DEG1\n"
        "\n"
        "By default, we render to a window. If --width is given, we render\n"
        "to an image (--image) and/or a binary range table (--ranges) instead.\n"
        "--height applies only if --width is given, and is optional; a reasonable\n"
        "field-of-view will be assumed if --height is omitted."
        "The image filename MUST be a .png file\n"
        "\n"
        "I load RENDER_RADIUS_CELLS of the DEM in each direction off the center\n"
        "point. If --radius is omitted, a reasonable default is chosen\n"
        "\n"
        "When plotting to a window, AZ_DEG are the azimuth bounds of the\n"
        "VIEWPORT. When rendering to an image, AZ_DEG are the\n"
        "centers of the first and last pixels. This is slightly smaller\n"
        "than the whole viewport: there's one extra pixel on each side\n"
        "\n"
        "The extents of ranges that we render are given by --zmin and --zmax,\n"
        "in meters. Anything closer than --zmin and further out than --zmax will\n"
        "not be rendered. The color-coding extents are given by --zmin-color and\n"
        "--zmax-color. Anything at or closer than --zmin-color will be rendered\n"
        "as black, and anything at or further than --zmax-color will be rendered\n"
        "as red. All 4 of these have reasonable defaults, and may be omitted\n"
        "\n"
        "By default we colorcode the renders by range. If --texture, we\n"
        "use a set of image tiles to texture the render instead\n"
        "\n"
        "The DEMs are in the directory given by --dirdems, or in\n"
        "~/.horizonator/DEMs_SRTM3/ if omitted.\n"
        "\n"
        "The tiles are in the directory given by --dirtiles, or in\n"
        "~/.horizonator/tiles if omitted.\n";

    struct option opts[] = {
        { "width",             required_argument, NULL, 'w' },
        { "height",            required_argument, NULL, 'H' },
        { "image",             required_argument, NULL, 'i' },
        { "radius",            required_argument, NULL, 'R' },
        { "ranges",            required_argument, NULL, 'r' },
        { "dirdems",           required_argument, NULL, 'd' },
        { "dirtiles",          required_argument, NULL, 't' },
        { "texture",           no_argument,       NULL, 'T' },
        { "allow-tile-downloads",no_argument,     NULL, 'a' },
        { "znear",             required_argument, NULL, '1' },
        { "zfar",              required_argument, NULL, '2' },
        { "znear-color",       required_argument, NULL, '3' },
        { "zfar-color",        required_argument, NULL, '4' },
        { "help",              no_argument,       NULL, 'h' },
        {}
    };

    int         width           = 0;
    int         height          = 0;
    const char* filename_image  = NULL;
    const char* filename_ranges = NULL;
    const char* dir_dems        = NULL;
    const char* dir_tiles       = NULL;
    bool        render_texture  = false;
    bool        allow_downloads = false;
    int         render_radius_cells = 1000;

    float znear       = -1.0f;
    float zfar        = -1.0f;
    float znear_color = -1.0f;
    float zfar_color  = -1.0f;

    int opt;
    do
    {
        // "h" means -h does something
        opt = getopt_long(argc, argv, "+h", opts, NULL);
        switch(opt)
        {
        case -1:
            break;

        case 'h':
            printf(usage, argv[0]);
            return 0;

        case 'w':
            width = atoi(optarg);
            if(width <= 0)
            {
                fprintf(stderr, "--width must have an integer argument > 0\n");
                return 1;
            }
            break;

        case 'H':
            height = atoi(optarg);
            if(height <= 0)
            {
                fprintf(stderr, "--height must have an integer argument > 0\n");
                return 1;
            }
            break;

        case 'R':
            render_radius_cells = atoi(optarg);
            if(height <= 0)
            {
                fprintf(stderr, "--radius must have an integer argument > 0\n");
                return 1;
            }
            break;

        case '1':
            znear = (float)atof(optarg);
            if(znear <= 0.0f)
            {
                fprintf(stderr, "--znear must have an float argument > 0\n");
                return 1;
            }
            break;
        case '2':
            zfar = (float)atof(optarg);
            if(zfar <= 0.0f)
            {
                fprintf(stderr, "--zfar must have an float argument > 0\n");
                return 1;
            }
            break;
        case '3':
            znear_color = (float)atof(optarg);
            if(znear_color <= 0.0f)
            {
                fprintf(stderr, "--znear-color must have an float argument > 0\n");
                return 1;
            }
            break;
        case '4':
            zfar_color = (float)atof(optarg);
            if(zfar_color <= 0.0f)
            {
                fprintf(stderr, "--zfar-color must have an float argument > 0\n");
                return 1;
            }
            break;

        case 'i':
            filename_image = optarg;
            break;

        case 'r':
            filename_ranges = optarg;
            break;

        case 'd':
            dir_dems = optarg;
            break;

        case 't':
            dir_tiles = optarg;
            break;

        case 'T':
            render_texture = true;
            break;

        case 'a':
            allow_downloads = true;
            break;

        case '?':
            fprintf(stderr, "Unknown option\n\n");
            fprintf(stderr, usage, argv[0]);
            return 1;
        }
    } while( opt != -1 );

    int Nargs_remaining = argc-optind;
    if( Nargs_remaining != 4 )
    {
        fprintf(stderr, "Need exactly 4 non-option arguments. Got %d\n\n",Nargs_remaining);
        fprintf(stderr, usage, argv[0]);
        return 1;
    }

    if(width >  0 && !(filename_image != NULL || filename_ranges != NULL))
    {
        fprintf(stderr, "--width makes sense only with (--image or --ranges)\n\n");
        fprintf(stderr, usage, argv[0]);
        return 1;
    }
    if(width <= 0 &&  (filename_image != NULL || filename_ranges != NULL))
    {
        fprintf(stderr, "--width required if (--image or --ranges)\n\n");
        fprintf(stderr, usage, argv[0]);
        return 1;
    }
    if( height > 0 && width <= 0 )
    {
        fprintf(stderr, "--height makes sense only with --width\n\n");
        fprintf(stderr, usage, argv[0]);
        return 1;
    }

    if(filename_image != NULL)
    {
        int l = strlen(filename_image);
        if(l < 5 || 0 != strcasecmp(".png", &filename_image[l-4]))
        {
            fprintf(stderr, "--image MUST be given a '.png' filename\n\n");
            fprintf(stderr, usage, argv[0]);
            return 1;
        }
    }

    float lat     = (float)atof(argv[optind+0]);
    float lon     = (float)atof(argv[optind+1]);
    float az_deg0 = (float)atof(argv[optind+2]);
    float az_deg1 = (float)atof(argv[optind+3]);

    if( lat < -80.f  || lat > 80.f )
    {
        fprintf(stderr, "Got invalid latitude");
        return false;

    }
    if( lon < -180.f || lon > 180.f )
    {
        fprintf(stderr, "Got invalid longitude");
        return false;

    }

    if(az_deg0 > az_deg1)
    {
        fprintf(stderr, "MUST have az_deg0 < az_deg1\n");
        return 1;
    }

    if(filename_image == NULL && filename_ranges == NULL)
    {
        horizonator_allinone_glut_loop(render_texture,
                                       lat, lon, az_deg0, az_deg1,
                                       render_radius_cells,
                                       znear,zfar,znear_color,zfar_color,
                                       dir_dems, dir_tiles,
                                       allow_downloads);
        return 0;
    }


    // The user gave me az_deg referring to the center of the pixels at the
    // edge. I need to convert them to represent the edges of the viewport.
    // That's 0.5 pixels extra on either side
    float az_per_pixel = (az_deg1 - az_deg0) / (float)(width-1);
    az_deg0 -= az_per_pixel/2.f;
    az_deg1 += az_per_pixel/2.f;

    if(height <= 0)
    {
        // Assume a 20deg fov if no height requested
        const float fovy_deg = 20.0f;
        height = (int)roundf( (float)width * fovy_deg / (az_deg1-az_deg0));
    }

    char*  image  = NULL;
    float* ranges = NULL;

    if(filename_image != NULL)
    {
        image = malloc( width*height * 3 );
        if(image == NULL)
        {
            MSG("image buffer malloc() failed");
            return 1;
        }
    }

    if(filename_ranges != NULL)
    {
        ranges = malloc( width*height * sizeof(float) );
        if(ranges == NULL)
        {
            MSG("ranges buffer malloc() failed");
            return 1;
        }
    }


    horizonator_context_t ctx;
    if( !horizonator_init( &ctx,
                           lat, lon,
                           width, height,
                           render_radius_cells,
                           true,
                           render_texture,
                           dir_dems,
                           dir_tiles,
                           allow_downloads) )
    {
        fprintf(stderr, "horizonator_init() failed\n");
        return false;
    }

    if(!horizonator_set_zextents(&ctx,
                                 znear, zfar, znear_color, zfar_color))
        return false;

    if(!horizonator_pan_zoom( &ctx, az_deg0, az_deg1))
    {
        fprintf(stderr, "horizonator_pan_zoom() failed");
        return false;
    }

    if(!horizonator_render_offscreen(&ctx, image, ranges))
    {
        fprintf(stderr, "render failed\n");
        return 1;
    }

    if(filename_image != NULL)
    {
        FreeImage_Initialise(true);
        FIBITMAP* fib = FreeImage_ConvertFromRawBits((BYTE*)image, width, height,
                                                     3*width, 24,
                                                     0,0,0,
                                                     // Top row is stored first
                                                     true);

        if(!FreeImage_Save(FIF_PNG, fib, filename_image, 0))
        {
            fprintf(stderr, "Couldn't save to '%s'\n", filename_image);
            return 1;
        }
        FreeImage_Unload(fib);
        FreeImage_DeInitialise();
        free(image);
    }

    if(filename_ranges != NULL)
    {
        FILE* fp = fopen(filename_ranges, "w");
        if(fp == NULL)
        {
            MSG("Cannot open ranges image at '%s'. Giving up", filename_ranges);
            return 1;
        }

        if((unsigned int)(width*height) != fwrite(ranges,
                                                  sizeof(float), width*height, fp))
        {
            MSG("Failed to write complete 'ranges' data");
            return 1;
        }
        fclose(fp);
    }

    return 0;
}
