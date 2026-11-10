/*
 * Particles of the SA-MP effect objects, as the game shows them: the particle systems of the game
 * (models\effects.fxp) with the textures of models\effectsPC.txd, played as FxSystem_c plays them.
 * Ported from model-preview (FxProject.cpp, Particles.cpp), drawn with the OpenGL 1.x of the editor.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* An effect model the editor read from an IMG (the DFF name, as in the archive), its particle
   2dEffect: name of the particle system and its position in the model */
void fx_note_model(const char *dffname, const char *particle, const float offset[3]);

/* At the end of TForm1's scene drawing (0x4E3D1C), before the selection is read: the particles of
   the effect objects near the camera, drawn in the scene, or given to the selection when the editor
   picks an object (GL_SELECT) */
void fx_after_scene(void);

/* Holds fx_look_at, the gluLookAt of the editor's scene that keeps the view for the particles: the
   editor's "mov eax,[slot]; mov eax,[eax]; call eax" at 0x4E1E94 is given its address */
extern void *fx_look_at_slot;

#ifdef __cplusplus
}
#endif
