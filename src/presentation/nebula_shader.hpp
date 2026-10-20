const char *fog_functions = R"GLSL(
uniform int fogCount;
uniform vec4 fogBounds[32]; // centre x/z, radius, inner radius
uniform vec4 fogExtents[32]; // half width, half length, vertical half height, shape
uniform vec4 fogColours[32]; // rgb and density/m
uniform vec3 fogOrigin; // world origin relative to camera rebase
vec2 fogSlab(float p,float d,float halfSize){
 if(abs(d)<.0000001)return abs(p)<=halfSize?vec2(-1e20,1e20):vec2(1,-1);
 vec2 t=vec2(-halfSize-p,halfSize-p)/d;return vec2(min(t.x,t.y),max(t.x,t.y));
}
vec2 fogCircle(vec2 p,vec2 d,float radius){
 float a=dot(d,d);if(a<.0000001)return dot(p,p)<=radius*radius?vec2(-1e20,1e20):vec2(1,-1);
 float b=dot(p,d);float disc=b*b-a*(dot(p,p)-radius*radius);
 if(disc<0.0)return vec2(1,-1);
 float q=sqrt(disc);return vec2((-b-q)/a,(-b+q)/a);
}
float fogSpan(vec2 a,vec2 b,float distance){
 return max(0.0,min(distance,min(a.y,b.y))-max(0.0,max(a.x,b.x)));
}
vec4 fogAlong(vec3 start,vec3 end){
 vec3 delta=end-start;float distance=length(delta);if(distance<.00001)return vec4(0);
 vec3 d=delta/distance;float total=0.0;vec3 colour=vec3(0);
 for(int i=0;i<32;i++){
  if(i>=fogCount)break;
  vec3 p=start-fogOrigin-vec3(fogBounds[i].x,0,fogBounds[i].y);
  vec2 vertical=fogSlab(p.y,d.y,fogExtents[i].z);
  vec2 plane;
  if(fogExtents[i].w==1.0){
   vec2 x=fogSlab(p.x,d.x,fogExtents[i].x),z=fogSlab(p.z,d.z,fogExtents[i].y);
   plane=vec2(max(x.x,z.x),min(x.y,z.y));
  }else plane=fogCircle(p.xz,d.xz,fogBounds[i].z);
  float span=fogSpan(plane,vertical,distance);
  if(fogExtents[i].w==2.0)span=max(0.0,span-fogSpan(fogCircle(p.xz,d.xz,fogBounds[i].w),vertical,distance));
  float depth=span*fogColours[i].w;
  total+=depth;colour+=fogColours[i].rgb*depth;
 }
 return total>0.0?vec4(colour/total,1.0-exp(-total)):vec4(0);
}
vec4 applyFog(vec4 colour,vec3 start,vec3 end){vec4 f=fogAlong(start,end);return vec4(mix(colour.rgb,f.rgb,f.a),colour.a);}
)GLSL";
const char *fog_background_vertex = R"GLSL(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
out vec2 uv;
void main(){uv=vertexTexCoord;gl_Position=vec4(uv*2.0-1.0,1.0,1.0);}
)GLSL";
const char *fog_background_fragment = R"GLSL(
in vec2 uv;
out vec4 finalColor;
uniform mat4 inverseViewProjection;
uniform sampler2D texture0;
uniform int hasSkybox;
void main(){
 vec4 a=inverseViewProjection*vec4(uv*2.0-1.0,-1,1);
 vec4 b=inverseViewProjection*vec4(uv*2.0-1.0,1,1);
 vec3 start=a.xyz/a.w,end=b.xyz/b.w;
 vec4 fog=fogAlong(start,end);
 if(hasSkybox==1){
  vec3 direction=normalize(end-start);
  vec2 skyUV=vec2(atan(direction.z,direction.x)/6.28318530718+.5,
                  acos(clamp(direction.y,-1.0,1.0))/3.14159265359);
  float pole=.5/float(textureSize(texture0,0).y);
  skyUV.y=clamp(skyUV.y,pole,1.0-pole);
  vec3 sky=texture(texture0,skyUV).rgb;
  finalColor=vec4(mix(sky,fog.rgb,fog.a),1.0);
 }else finalColor=vec4(fog.rgb,fog.a);
}
)GLSL";
