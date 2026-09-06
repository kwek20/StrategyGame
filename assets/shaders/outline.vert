#version 450 core
layout(location = 0) in vec3 inPosition;
layout(location = 3) in ivec4 inBones;
layout(location = 4) in vec4 inWeights;
uniform mat4 viewProjection;
uniform mat4 model;
uniform bool useSkinning;
uniform mat4 bones[100];
void main() {
    mat4 skin = mat4(1.0);
    if (useSkinning) {
        skin = mat4(0.0);
        for (int i = 0; i < 4; ++i)
            if (inBones[i] >= 0) skin += bones[inBones[i]] * inWeights[i];
    }
    gl_Position = viewProjection * model * skin * vec4(inPosition, 1.0);
}
