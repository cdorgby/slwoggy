#version 330

// Input vertex attributes (from vertex shader)
in vec2 fragTexCoord;
in vec4 fragColor;

// Input uniform values
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float time;
uniform vec2 resolution;

// Output fragment color
out vec4 finalColor;

void main()
{
    // Pixelation effect
    float pixelSize = 8.0 + sin(time) * 4.0; // Animate pixel size
    
    vec2 uv = fragTexCoord;
    vec2 pixels = resolution / pixelSize;
    
    // Snap to pixel grid
    uv = floor(uv * pixels) / pixels;
    
    // Get pixelated color
    vec4 texelColor = texture(texture0, uv);
    
    // Add some retro color reduction
    vec3 color = texelColor.rgb;
    color = floor(color * 4.0) / 4.0;
    
    // Add scanline effect
    float scanline = sin(fragTexCoord.y * resolution.y * 2.0) * 0.04;
    color -= scanline;
    
    finalColor = vec4(color, texelColor.a) * colDiffuse * fragColor;
}