#version 330

// Input vertex attributes (from vertex shader)
in vec2 fragTexCoord;
in vec4 fragColor;

// Input uniform values
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform float time;

// Output fragment color
out vec4 finalColor;

void main()
{
    // Create a wave effect
    vec2 uv = fragTexCoord;
    uv.x += sin(uv.y * 10.0 + time * 2.0) * 0.05;
    uv.y += cos(uv.x * 10.0 + time * 2.0) * 0.03;
    
    // Get texture color with wave distortion
    vec4 texelColor = texture(texture0, uv);
    
    // Apply some color shifting based on time
    vec3 color = texelColor.rgb;
    color.r *= abs(sin(time));
    color.g *= abs(cos(time * 0.7));
    color.b *= abs(sin(time * 1.3));
    
    finalColor = vec4(color, texelColor.a) * colDiffuse * fragColor;
}