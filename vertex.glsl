/* -*- c -*- */

/* Makefile includes vertex_header.glsl here */


void main(void)
{
  /* gl_Vertex is (i,j,height) */
  bool at_left_seam  = false;
  bool at_right_seam = false;
  vec3 vin = gl_Vertex.xyz;
  if( vin.x < 0.0 )
  {
    vin.x += float(2*WDEM);
    at_left_seam = true;
  }
  else if( vin.y < 0.0 )
  {
    vin.y += float(2*WDEM);
    at_right_seam = true;
  }

  float lon = radians( float(-demfileW)     + vin.x/float(WDEM-1) );
  float lat = radians( float( demfileN + 1) - vin.y/float(WDEM-1) );

  // Here I compute 4 sin/cos. Previously I was sending sincos( view_lat/lon) as
  // a uniform, so no trig was needed here. I think this may have been causing
  // roundoff issues, so I'm not doing that anymore. Specifically, sin(+eps) was
  // being slightly negative
  float sin_dlat = sin( lat - view_lat );
  float cos_dlat = cos( lat - view_lat );
  float sin_dlon = sin( lon - view_lon );
  float cos_dlon = cos( lon - view_lon );

  float sin_lat  = sin( lat );
  float cos_lat  = cos( lat );

  // Convert current point being rendered into the coordinate system centered on
  // the viewpoint. The axes are (east,north,height). I implicitly divide all 3
  // by the height of the observation point
  float east   = cos_lat * sin_dlon;
  float north  = sin_dlat*cos_dlon + sin_lat*cos_view_lat*(1.0 - cos_dlon);
  float height = cos_dlat*cos_dlon + sin_lat*sin_view_lat*(1.0 - cos_dlon)
    - (Rearth + view_z) / (Rearth + vin.z);

  float len_ne = length(vec2(east, north ));
  float zeff  = (Rearth + vin.z)*len_ne;
  float az    = atan(east, north) / pi;
  float el    = atan( height, len_ne ) / pi;

  if     ( at_left_seam )  az -= 2.0;
  else if( at_right_seam ) az += 2.0;

  // coloring by...
  red = zeff / 10000.0; // ... distance from camera
  //red = vin.z / 3500.0; // ... elevation

  // float ft = vin.z*100.0/2.54/12.0;
  // if( abs( float(-demfileW)+ vin.x/float(WDEM-1) -
  //         -118.18) < 0.0005 )
  //   red = 1.0;

  const float A = (zfar + znear) / (zfar - znear);
  gl_Position = vec4( az * zeff,
                      el * zeff * aspect,
                      mix(zfar, zeff, A),
                      zeff );
}
