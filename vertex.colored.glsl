/* -*- c -*- */

#version 110

// We receive these from the CPU code
uniform float viewer_cell_i, viewer_cell_j;
uniform float viewer_z;
uniform float DEG_PER_CELL;
uniform float view_lon, view_lat;
uniform float sin_viewer_lat, cos_viewer_lat;
uniform float aspect;

// We send these to the fragment shader
varying float channel_distance, channel_elevation, channel_griddist;

const float Rearth = 6371000.0;
const float pi     = 3.14159265358979;

// these define the front and back clipping planes, in meters
const float znear = 100.0;
const float zfar  = 20000.0;

// Past this distance the render color doesn't change, in meters
const float zfar_color = 40000.0;

void main(void)
{
    /*
      I do this in the tangent plane, ignoring the spherical (and even
      ellipsoidal) nature of the Earth. It is close-enough. Python script to
      confirm:

        import numpy as np

        d = 20000.
        R = 6371000.0

        th = d/R
        s  = np.sin(th)
        c  = np.cos(th)

        x_plane  = np.array((d,R))
        x_sphere = np.array((s,c))*R

        print(x_plane - x_sphere)

      says: [ 0.03284909 31.39222034]. So at 20km out, this planar assumption
      produces 30m of error, primarily in the vertical direction. This
      admittedly is 1.5mrad. Which is marginally too much. But 20km is quite a
      lot. At 10km the error is 7.8m, which is 0.78mrad. I should do something
      better, but in the near-term this is more than good-enough.
     */



    // Seam stuff. First, handle the cell the viewer is sitting on
    if( gl_Vertex.x < 0.0 && gl_Vertex.y < 0.0 )
    {
        // x and y <0 means this is either the bottom-left of screen or bottom-right
        // of screen. The choice between these two is the sign of vin.z
        if( gl_Vertex.z < 0.0 )
        {
            gl_Position = vec4( -1.0, -1.0,
                                -1.0,  1.0 );
        }
        else
        {
            gl_Position = vec4( +1.0, -1.0,
                                -1.0,  1.0 );
        }
        channel_distance  = 0.0;
        channel_elevation = 0.5;
        return;
    }


    float distance_ne;

    // Several different paths exist for the data processing, with different
    // amounts of the math done in the CPU or GPU. The corresponding path must
    // be selected in the CPU code in init.c
    if(false)
    {
        distance_ne = gl_Vertex.z;
        gl_Position = vec4( gl_Vertex.x,
                            gl_Vertex.y * aspect,
                            (distance_ne - znear) / (zfar - znear) * 2. - 1.,
                            1.0 );
    }
    else if(false)
    {
        distance_ne = length(gl_Vertex.xy);
        gl_Position = vec4( atan(gl_Vertex.x, gl_Vertex.y) / pi,
                            atan(gl_Vertex.z, distance_ne) / pi * aspect,
                            (distance_ne - znear) / (zfar - znear) * 2. - 1.,
                            1.0 );
    }
    else
    {
        // This is the only path that supports the seam business
        bool at_left_seam  = false;
        bool at_right_seam = false;

        float i = gl_Vertex.x;
        float j = gl_Vertex.y;
        if( i < 0.0 )
        {
            i = -i - 1.0; // extra 1 because I can't assume that -0 < 0
            at_left_seam = true;
        }
        else if( j < 0.0 )
        {
            j = -j - 1.0; // extra 1 because I can't assume that -0 < 0
            at_right_seam = true;
        }

        vec2 en =
            vec2( (i - viewer_cell_i) * DEG_PER_CELL * Rearth * pi/180. * cos_viewer_lat,
                  (j - viewer_cell_j) * DEG_PER_CELL * Rearth * pi/180. );

        distance_ne = length(en);
        float az = atan(en.x, en.y) / pi;
        if     ( at_left_seam )  az -= 2.0;
        else if( at_right_seam ) az += 2.0;

        gl_Position = vec4( az,
                            atan(gl_Vertex.z - viewer_z, distance_ne) / pi * aspect,
                            (distance_ne - znear) / (zfar - znear) * 2. - 1.,
                            1.0 );
    }

    channel_distance  = distance_ne / zfar;
    channel_griddist  = 0.0;

}
