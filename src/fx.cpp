/*
 * Particles of the SA-MP effect objects (fire, smoke, explosions, water...), as the game shows them.
 *
 * An effect object of SA-MP is a DFF of one 1 mm triangle and a particle 2dEffect naming a particle
 * system of the game. The game plays that system (models\effects.fxp) with the textures of
 * models\effectsPC.txd; so does this file, ported from model-preview (src/formats/dff/FxProject.cpp
 * and src/viewer/Particles.cpp, after FxSystem_c of gta-reversed), drawn with the fixed-function
 * OpenGL 1.x the editor draws with, in its context and on its thread.
 *
 * - The view: the modelview matrix right after the editor's gluLookAt (its import slot pointer is
 *   swapped for fx_look_at at 0x4E1E94).
 * - The effect objects: the instances of the map whose model is one of the effect DFFs the editor
 *   read (fx_note_model, from the IMG hook), placed and turned as the editor draws them.
 * - The particles move in the world: they are made at the effect, turned with the object, and then
 *   fall, rise and drift on their own, as in the game.
 * - At the end of the scene (0x4E3D1C) they are drawn over it (depth tested, not written), blended
 *   ones back to front, then the added ones. When the editor picks an object (GL_SELECT) they are
 *   drawn instead under the name of their object, so a click on the fire selects the fire object.
 */
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "fx.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace
{
/* ---------- editor addresses (image base 0x400000, no ASLR) ---------- */

const DWORD GAME_DIR = 0x578DD4;   /* AnsiString: the game folder                                   */
const DWORD MAP_PTR = 0x578EF4;    /* the map: +0x3C dynamic array of IPLs, each +8 its instances    */
const DWORD IDE_BY_ID = 0x57A5B8;  /* array[0..19999] of the IDE definition (+8 model name), or nil */
const int IDE_MAX_ID = 19999;
const DWORD GLU_LOOKAT = 0x4FF568; /* import slot: address of the editor's gluLookAt pointer        */
const DWORD FN_AXIS_MATRIX = 0x4DCA00; /* matrix of an axis and angle (eax = 16 floats; angle, x, y, z
                                          pushed, ret 10h): how the editor turns an instance          */

/* An instance of an IPL as the editor keeps it */
const int INST_MODEL = 0x04;  /* int                                                    */
const int INST_POS = 0x18;    /* float x, y, z                                          */
const int INST_QUAT = 0x24;   /* float x, y, z, w                                       */
const int INST_GONE = 0x3B;   /* byte: removed or hidden when not 0                     */

/* ---------- limits ---------- */

const float DRAW_DISTANCE = 200.0f;  /* effects further from the camera show only their dot */
const size_t MAX_EFFECTS = 96;       /* the nearest ones play                                */
const float STEP = 1.0f / 30.0f;     /* steps of the system time: the frames of the game     */
const float PI = 3.14159265358979f;
const float WIND_SPEED = 0.25f;      /* a light wind of a clear day (FxInfoWind_c)           */

/* ---------- OpenGL 1.1 of opengl32.dll ---------- */

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef float GLfloat;

const GLenum GL_MODELVIEW = 0x1700, GL_PROJECTION = 0x1701, GL_TEXTURE = 0x1702;
const GLenum GL_MODELVIEW_MATRIX = 0x0BA6, GL_RENDER_MODE = 0x0C40, GL_SELECT = 0x1C02;
const GLenum GL_TEXTURE_2D = 0x0DE1, GL_BLEND = 0x0BE2, GL_DEPTH_TEST = 0x0B71, GL_LIGHTING = 0x0B50;
const GLenum GL_CULL_FACE = 0x0B44, GL_ALPHA_TEST = 0x0BC0, GL_COLOR_MATERIAL = 0x0B57;
const GLenum GL_TEXTURE_GEN_S = 0x0C60, GL_TEXTURE_GEN_T = 0x0C61, GL_TEXTURE_GEN_R = 0x0C62,
             GL_TEXTURE_GEN_Q = 0x0C63;
const GLenum GL_ZERO = 0, GL_ONE = 1, GL_SRC_COLOR = 0x0300, GL_ONE_MINUS_SRC_COLOR = 0x0301,
             GL_SRC_ALPHA = 0x0302, GL_ONE_MINUS_SRC_ALPHA = 0x0303, GL_DST_ALPHA = 0x0304,
             GL_ONE_MINUS_DST_ALPHA = 0x0305, GL_DST_COLOR = 0x0306, GL_ONE_MINUS_DST_COLOR = 0x0307,
             GL_SRC_ALPHA_SATURATE = 0x0308;
const GLenum GL_QUADS = 7, GL_RGBA = 0x1908, GL_UNSIGNED_BYTE = 0x1401, GL_UNPACK_ALIGNMENT = 0x0CF5;
const GLenum GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_MAG_FILTER = 0x2800, GL_LINEAR = 0x2601,
             GL_LINEAR_MIPMAP_LINEAR = 0x2703, GL_TEXTURE_WRAP_S = 0x2802, GL_TEXTURE_WRAP_T = 0x2803,
             GL_CLAMP_TO_EDGE = 0x812F, GL_TEXTURE_MAX_LEVEL = 0x813D;
const GLenum GL_TEXTURE_ENV = 0x2300, GL_TEXTURE_ENV_MODE = 0x2200, GL_MODULATE = 0x2100;
const GLenum GL_FRONT_AND_BACK = 0x0408, GL_FILL = 0x1B02, GL_ALL_ATTRIB_BITS = 0x000FFFFF;

struct Gl
{
  void(WINAPI* Begin)(GLenum);
  void(WINAPI* End)();
  void(WINAPI* Vertex3f)(GLfloat, GLfloat, GLfloat);
  void(WINAPI* TexCoord2f)(GLfloat, GLfloat);
  void(WINAPI* Color4f)(GLfloat, GLfloat, GLfloat, GLfloat);
  void(WINAPI* Enable)(GLenum);
  void(WINAPI* Disable)(GLenum);
  void(WINAPI* BlendFunc)(GLenum, GLenum);
  void(WINAPI* DepthMask)(unsigned char);
  void(WINAPI* BindTexture)(GLenum, GLuint);
  void(WINAPI* GenTextures)(GLint, GLuint*);
  void(WINAPI* TexImage2D)(GLenum, GLint, GLint, GLint, GLint, GLint, GLenum, GLenum, const void*);
  void(WINAPI* TexParameteri)(GLenum, GLenum, GLint);
  void(WINAPI* TexEnvi)(GLenum, GLenum, GLint);
  void(WINAPI* PixelStorei)(GLenum, GLint);
  void(WINAPI* MatrixMode)(GLenum);
  void(WINAPI* PushMatrix)();
  void(WINAPI* PopMatrix)();
  void(WINAPI* LoadMatrixf)(const GLfloat*);
  void(WINAPI* LoadIdentity)();
  void(WINAPI* PushAttrib)(unsigned int);
  void(WINAPI* PopAttrib)();
  void(WINAPI* GetFloatv)(GLenum, GLfloat*);
  void(WINAPI* GetIntegerv)(GLenum, GLint*);
  void(WINAPI* PolygonMode)(GLenum, GLenum);
  void(WINAPI* PushName)(GLuint);
  void(WINAPI* PopName)();
};

Gl g_gl;
bool g_gl_loaded, g_gl_ok;

bool LoadGl()
{
  if (g_gl_loaded)
  {
    return g_gl_ok;
  }
  g_gl_loaded = true;
  HMODULE gl = GetModuleHandleA("opengl32.dll");
  if (!gl)
  {
    return false;
  }
  bool ok = true;
  auto get = [&](auto& fn, const char* name)
  {
    FARPROC p = GetProcAddress(gl, name);
    if (!p)
    {
      ok = false;
    }
    fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(p);
  };
  get(g_gl.Begin, "glBegin");
  get(g_gl.End, "glEnd");
  get(g_gl.Vertex3f, "glVertex3f");
  get(g_gl.TexCoord2f, "glTexCoord2f");
  get(g_gl.Color4f, "glColor4f");
  get(g_gl.Enable, "glEnable");
  get(g_gl.Disable, "glDisable");
  get(g_gl.BlendFunc, "glBlendFunc");
  get(g_gl.DepthMask, "glDepthMask");
  get(g_gl.BindTexture, "glBindTexture");
  get(g_gl.GenTextures, "glGenTextures");
  get(g_gl.TexImage2D, "glTexImage2D");
  get(g_gl.TexParameteri, "glTexParameteri");
  get(g_gl.TexEnvi, "glTexEnvi");
  get(g_gl.PixelStorei, "glPixelStorei");
  get(g_gl.MatrixMode, "glMatrixMode");
  get(g_gl.PushMatrix, "glPushMatrix");
  get(g_gl.PopMatrix, "glPopMatrix");
  get(g_gl.LoadMatrixf, "glLoadMatrixf");
  get(g_gl.LoadIdentity, "glLoadIdentity");
  get(g_gl.PushAttrib, "glPushAttrib");
  get(g_gl.PopAttrib, "glPopAttrib");
  get(g_gl.GetFloatv, "glGetFloatv");
  get(g_gl.GetIntegerv, "glGetIntegerv");
  get(g_gl.PolygonMode, "glPolygonMode");
  get(g_gl.PushName, "glPushName");
  get(g_gl.PopName, "glPopName");
  g_gl_ok = ok;
  return ok;
}

/* ---------- vectors ---------- */

struct Vec3
{
  float X = 0, Y = 0, Z = 0;
};
Vec3 operator+(Vec3 a, Vec3 b) { return { a.X + b.X, a.Y + b.Y, a.Z + b.Z }; }
Vec3 operator-(Vec3 a, Vec3 b) { return { a.X - b.X, a.Y - b.Y, a.Z - b.Z }; }
Vec3 operator*(Vec3 a, float s) { return { a.X * s, a.Y * s, a.Z * s }; }
float Dot(Vec3 a, Vec3 b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }
Vec3 Cross(Vec3 a, Vec3 b) { return { a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X }; }
float Len(Vec3 v) { return std::sqrt(Dot(v, v)); }
Vec3 SafeNormalize(Vec3 v, Vec3 fallback)
{
  const float length = Len(v);
  return length > 1e-6f ? v * (1.0f / length) : fallback;
}

/* A 3x3 rotation, column-major like OpenGL: world = R * local */
struct Mat3
{
  float M[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
  Vec3 operator*(Vec3 v) const
  {
    return { M[0] * v.X + M[3] * v.Y + M[6] * v.Z, M[1] * v.X + M[4] * v.Y + M[7] * v.Z,
      M[2] * v.X + M[5] * v.Y + M[8] * v.Z };
  }
};

std::string Lower(std::string s)
{
  for (char& c : s)
  {
    if (c >= 'A' && c <= 'Z')
    {
      c = static_cast<char>(c + 32);
    }
  }
  return s;
}

bool ReadWholeFile(const std::string& path, std::string& out)
{
  HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE)
  {
    return false;
  }
  DWORD size = GetFileSize(f, nullptr), read = 0;
  bool ok = size != INVALID_FILE_SIZE && size < 64u * 1024 * 1024;
  if (ok)
  {
    out.resize(size);
    ok = size == 0 || (ReadFile(f, &out[0], size, &read, nullptr) && read == size);
  }
  CloseHandle(f);
  return ok;
}

/* ---------- effects.fxp (FxProject.cpp of model-preview) ---------- */

struct FxChannel
{
  bool Looped = false;
  std::vector<float> Times, Values;

  /* the keys linearly, looped over the last key when the channel loops (FxInterpInfoFloat_c::GetVal) */
  float At(float time) const
  {
    const size_t count = std::min(Times.size(), Values.size());
    if (count == 0)
    {
      return 0;
    }
    const size_t last = count - 1;
    if (last == 0)
    {
      return Values[0];
    }
    if (Looped && Times[last] > 0)
    {
      time -= std::floor(time / Times[last]) * Times[last];
    }
    if (time <= Times[0])
    {
      return Values[0];
    }
    for (size_t k = 1; k <= last; k++)
    {
      if (time < Times[k])
      {
        const float span = Times[k] - Times[k - 1];
        const float t = span > 0 ? (time - Times[k - 1]) / span : 1.0f;
        return Values[k - 1] + (Values[k] - Values[k - 1]) * t;
      }
    }
    return Values[last];
  }
};

enum FxInfoType
{
  EmRate, EmSize, EmSpeed, EmDir, EmAngle, EmLife, EmPos, EmRotation, Force, Friction, Wind, Noise,
  Jitter, AttractPt, RotSpeed, Colour, ColourBright, Size, SpriteRect, AnimTex, Dir, Flat, SelfLit,
  HeatHaze, InfoCount
};

const char* const INFO_NAMES[InfoCount] = { "EMRATE", "EMSIZE", "EMSPEED", "EMDIR", "EMANGLE", "EMLIFE",
  "EMPOS", "EMROTATION", "FORCE", "FRICTION", "WIND", "NOISE", "JITTER", "ATTRACTPT", "ROTSPEED", "COLOUR",
  "COLOURBRIGHT", "SIZE", "SPRITERECT", "ANIMTEX", "DIR", "FLAT", "SELFLIT", "HEATHAZE" };

bool IsMovement(int type)
{
  return type == Force || type == Friction || type == Wind || type == Noise || type == Jitter ||
    type == AttractPt;
}

struct FxInfo
{
  bool Present = false;
  bool ParticleTime = true; /* its time: the life of the particle (0-1), else the system time */
  std::vector<FxChannel> Channels;

  void At(float time, float* values, size_t count) const
  {
    for (size_t c = 0; c < count; c++)
    {
      values[c] = c < Channels.size() ? Channels[c].At(time) : 0.0f;
    }
  }
};

struct Texture
{
  int Width = 0, Height = 0;
  std::vector<unsigned char> RGBA;
  GLuint Name = 0; /* OpenGL texture, made on first use */
};

struct FxEmitter
{
  bool HasMatrix = false;
  float Matrix[12] = {}; /* right, up, at, position in the system */
  std::vector<std::string> TextureNames;
  std::vector<Texture*> Textures;
  int SourceBlend = 4, DestinationBlend = 5;
  FxInfo Infos[InfoCount];
  std::vector<int> Movement; /* the movement infos in the order of the file */

  const FxInfo& Info(int type) const { return Infos[type]; }
};

struct FxSystem
{
  std::string Name;
  float Length = 1, LoopIntervalMin = 0, LoopLength = 0;
  int PlayMode = 0; /* 0: plays once */
  std::vector<FxEmitter> Emitters;
};

struct Lines
{
  std::vector<std::string> Items;
  size_t At = 0;

  bool Done() const { return At >= Items.size(); }
  const std::string& Line() const { return Items[At]; }
  std::string Key() const
  {
    const size_t colon = Line().find(':');
    return colon == std::string::npos ? Line() : Line().substr(0, colon);
  }
  std::string Value(size_t offset = 0) const
  {
    if (At + offset >= Items.size())
    {
      return {};
    }
    const std::string& line = Items[At + offset];
    const size_t colon = line.find(':');
    if (colon == std::string::npos)
    {
      return {};
    }
    const size_t start = line.find_first_not_of(" \t", colon + 1);
    return start == std::string::npos ? std::string() : line.substr(start);
  }
  float Number(size_t offset = 0) const { return std::strtof(Value(offset).c_str(), nullptr); }
};

FxEmitter ReadEmitter(Lines& lines)
{
  FxEmitter emitter;
  while (!lines.Done() && lines.Line() != "FX_PRIM_BASE_DATA:")
  {
    lines.At++;
  }
  lines.At += 2; /* the base data, NAME */
  {
    const std::string matrix = lines.Value();
    const char* p = matrix.c_str();
    for (float& value : emitter.Matrix)
    {
      char* end;
      value = std::strtof(p, &end);
      p = end;
    }
    static const float identity[12] = { 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 };
    emitter.HasMatrix = std::memcmp(emitter.Matrix, identity, sizeof identity) != 0;
  }
  lines.At++;
  while (!lines.Done() && lines.Key().rfind("TEXTURE", 0) == 0)
  {
    const std::string texture = lines.Value();
    if (!texture.empty() && texture != "NULL")
    {
      emitter.TextureNames.push_back(Lower(texture));
    }
    lines.At++;
  }
  lines.At++; /* ALPHAON */
  emitter.SourceBlend = static_cast<int>(lines.Number());
  lines.At++;
  emitter.DestinationBlend = static_cast<int>(lines.Number());
  lines.At++;
  const int infoCount = static_cast<int>(lines.Number());
  lines.At++;
  for (int n = 0; n < infoCount && !lines.Done(); n++)
  {
    /* FX_INFO_EMRATE_DATA: => EMRATE */
    const std::string line = lines.Line();
    const std::string name = line.size() > 14 ? line.substr(8, line.size() - 14) : std::string();
    lines.At++;
    FxInfo info;
    info.Present = true;
    if (!lines.Done() && lines.Key() == "TIMEMODEPRT")
    {
      info.ParticleTime = lines.Number() != 0;
      lines.At++;
    }
    while (!lines.Done() && lines.Line().rfind("FX_INFO_", 0) != 0 && lines.Key() != "LODSTART")
    {
      if (lines.Line() == "FX_INTERP_DATA:")
      {
        FxChannel channel;
        channel.Looped = lines.Number(1) != 0;
        const int keys = static_cast<int>(lines.Number(2));
        lines.At += 3;
        for (int k = 0; k < keys && !lines.Done(); k++)
        {
          /* FX_KEYFLOAT_DATA:, TIME:, VAL: */
          channel.Times.push_back(lines.Number(1));
          channel.Values.push_back(lines.Number(2));
          lines.At += 3;
        }
        info.Channels.push_back(std::move(channel));
        continue;
      }
      lines.At++;
    }
    for (int type = 0; type < InfoCount; type++)
    {
      if (name == INFO_NAMES[type])
      {
        emitter.Infos[type] = std::move(info);
        if (IsMovement(type))
        {
          emitter.Movement.push_back(type);
        }
        break;
      }
    }
  }
  lines.At += 2; /* LODSTART, LODEND */
  return emitter;
}

std::map<std::string, std::unique_ptr<FxSystem>> ParseFxProject(const std::string& text)
{
  Lines lines;
  size_t pos = 0;
  while (pos < text.size())
  {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos)
    {
      end = text.size();
    }
    const std::string line = text.substr(pos, end - pos);
    const size_t a = line.find_first_not_of(" \t\r"), b = line.find_last_not_of(" \t\r");
    if (a != std::string::npos)
    {
      lines.Items.push_back(line.substr(a, b - a + 1));
    }
    pos = end + 1;
  }

  std::map<std::string, std::unique_ptr<FxSystem>> project;
  while (!lines.Done())
  {
    if (lines.Line() != "FX_SYSTEM_DATA:")
    {
      lines.At++;
      continue;
    }
    lines.At += 2; /* the version */
    if (!lines.Done() && lines.Key() == "FILENAME")
    {
      lines.At++;
    }
    std::unique_ptr<FxSystem> system(new FxSystem);
    system->Name = Lower(lines.Value());
    system->Length = lines.Number(1);
    system->LoopIntervalMin = lines.Number(2);
    system->LoopLength = lines.Number(3);
    system->PlayMode = static_cast<int>(lines.Number(4));
    lines.At += 5;
    while (!lines.Done() && lines.Key() != "NUM_PRIMS")
    {
      lines.At++;
    }
    const int prims = static_cast<int>(lines.Number());
    lines.At++;
    for (int p = 0; p < prims && !lines.Done(); p++)
    {
      system->Emitters.push_back(ReadEmitter(lines));
    }
    if (!system->Name.empty())
    {
      project[system->Name] = std::move(system);
    }
  }
  return project;
}

/* ---------- effectsPC.txd: Direct3D 8/9 native textures (RwTexture.cpp of model-preview) ---------- */

DWORD Rd32(const std::string& b, size_t p) { DWORD v; std::memcpy(&v, b.data() + p, 4); return v; }

void Decode565(unsigned c, unsigned char out[3])
{
  out[0] = static_cast<unsigned char>(((c >> 11) & 0x1F) * 255 / 31);
  out[1] = static_cast<unsigned char>(((c >> 5) & 0x3F) * 255 / 63);
  out[2] = static_cast<unsigned char>((c & 0x1F) * 255 / 31);
}

void DecodeColorBlock(const unsigned char* block, bool dxt1, unsigned char colors[16][4])
{
  const unsigned c0 = block[0] | (block[1] << 8), c1 = block[2] | (block[3] << 8);
  unsigned char palette[4][4];
  Decode565(c0, palette[0]);
  Decode565(c1, palette[1]);
  palette[0][3] = palette[1][3] = 255;
  for (int i = 0; i < 3; i++)
  {
    if (c0 > c1 || !dxt1)
    {
      palette[2][i] = static_cast<unsigned char>((2 * palette[0][i] + palette[1][i]) / 3);
      palette[3][i] = static_cast<unsigned char>((palette[0][i] + 2 * palette[1][i]) / 3);
    }
    else
    {
      palette[2][i] = static_cast<unsigned char>((palette[0][i] + palette[1][i]) / 2);
      palette[3][i] = 0;
    }
  }
  palette[2][3] = 255;
  palette[3][3] = (c0 > c1 || !dxt1) ? 255 : 0;
  const DWORD indices = block[4] | (block[5] << 8) | (block[6] << 16) | (static_cast<DWORD>(block[7]) << 24);
  for (int i = 0; i < 16; i++)
  {
    std::memcpy(colors[i], palette[(indices >> (2 * i)) & 3], 4);
  }
}

bool DecodeDXT(const unsigned char* data, size_t size, DWORD fourcc, Texture& t)
{
  const char kind = static_cast<char>(fourcc >> 24);
  const size_t blockSize = kind == '1' ? 8 : 16;
  const int bx = (t.Width + 3) / 4, by = (t.Height + 3) / 4;
  if (static_cast<size_t>(bx) * by * blockSize > size)
  {
    return false;
  }
  t.RGBA.assign(static_cast<size_t>(t.Width) * t.Height * 4, 0);
  for (int y0 = 0; y0 < by; y0++)
  {
    for (int x0 = 0; x0 < bx; x0++)
    {
      const unsigned char* block = data + (static_cast<size_t>(y0) * bx + x0) * blockSize;
      unsigned char colors[16][4];
      if (kind == '1')
      {
        DecodeColorBlock(block, true, colors);
      }
      else
      {
        DecodeColorBlock(block + 8, false, colors);
        if (kind == '3')
        {
          for (int i = 0; i < 16; i++)
          {
            colors[i][3] = static_cast<unsigned char>(((block[i / 2] >> ((i % 2) * 4)) & 0x0F) * 17);
          }
        }
        else
        {
          unsigned char a[8] = { block[0], block[1] };
          if (a[0] > a[1])
          {
            for (int i = 1; i < 7; i++)
            {
              a[i + 1] = static_cast<unsigned char>(((7 - i) * a[0] + i * a[1]) / 7);
            }
          }
          else
          {
            for (int i = 1; i < 5; i++)
            {
              a[i + 1] = static_cast<unsigned char>(((5 - i) * a[0] + i * a[1]) / 5);
            }
            a[6] = 0;
            a[7] = 255;
          }
          unsigned long long bits = 0;
          for (int i = 0; i < 6; i++)
          {
            bits |= static_cast<unsigned long long>(block[2 + i]) << (8 * i);
          }
          for (int i = 0; i < 16; i++)
          {
            colors[i][3] = a[(bits >> (3 * i)) & 7];
          }
        }
      }
      for (int py = 0; py < 4 && y0 * 4 + py < t.Height; py++)
      {
        for (int px = 0; px < 4 && x0 * 4 + px < t.Width; px++)
        {
          std::memcpy(&t.RGBA[((static_cast<size_t>(y0) * 4 + py) * t.Width + x0 * 4 + px) * 4],
            colors[py * 4 + px], 4);
        }
      }
    }
  }
  return true;
}

/* one Texture Native struct (platform 8 or 9), its first level */
bool ReadNativeTexture(const std::string& b, size_t p, size_t end, std::string& name, Texture& t)
{
  if (p + 92 > end)
  {
    return false;
  }
  const DWORD platform = Rd32(b, p);
  if (platform != 8 && platform != 9)
  {
    return false;
  }
  name = Lower(std::string(b.data() + p + 8, strnlen(b.data() + p + 8, 32)));
  const DWORD raster = Rd32(b, p + 72), alphaOrFormat = Rd32(b, p + 76);
  t.Width = static_cast<unsigned char>(b[p + 80]) | (static_cast<unsigned char>(b[p + 81]) << 8);
  t.Height = static_cast<unsigned char>(b[p + 82]) | (static_cast<unsigned char>(b[p + 83]) << 8);
  const unsigned char compression = static_cast<unsigned char>(b[p + 87]);
  if (t.Width <= 0 || t.Height <= 0 || t.Width > 4096 || t.Height > 4096)
  {
    return false;
  }
  DWORD fourcc = 0;
  if (platform == 8)
  {
    fourcc = compression == 1 ? 0x31545844 : compression == 3 ? 0x33545844 : compression == 5 ? 0x35545844 : 0;
  }
  else if ((compression & 8) || alphaOrFormat == 0x31545844 || alphaOrFormat == 0x33545844 ||
    alphaOrFormat == 0x35545844)
  {
    fourcc = alphaOrFormat;
  }
  size_t q = p + 88;
  const unsigned char* palette = nullptr;
  size_t paletteEntries = 0;
  if (raster & 0x6000)
  {
    paletteEntries = (raster & 0x2000) ? 256 : 32;
    if (q + paletteEntries * 4 > end)
    {
      return false;
    }
    palette = reinterpret_cast<const unsigned char*>(b.data() + q);
    q += paletteEntries * 4;
  }
  if (q + 4 > end)
  {
    return false;
  }
  const DWORD levelSize = Rd32(b, q);
  q += 4;
  if (q + levelSize > end)
  {
    return false;
  }
  const unsigned char* data = reinterpret_cast<const unsigned char*>(b.data() + q);
  if (fourcc)
  {
    return DecodeDXT(data, levelSize, fourcc, t);
  }

  const size_t pixels = static_cast<size_t>(t.Width) * t.Height;
  t.RGBA.assign(pixels * 4, 255);
  if (palette)
  {
    const bool pal4 = (raster & 0x4000) != 0;
    if ((pal4 ? (pixels + 1) / 2 : pixels) > levelSize)
    {
      return false;
    }
    for (size_t i = 0; i < pixels; i++)
    {
      size_t index = pal4 ? ((data[i / 2] >> ((i % 2) * 4)) & 0x0F) : data[i];
      std::memcpy(&t.RGBA[i * 4], palette + std::min(index, paletteEntries - 1) * 4, 4);
    }
    return true;
  }
  const DWORD format = raster & 0x0F00;
  const size_t bpp = (format == 0x0500 || format == 0x0600) ? 4 : (format == 0x0400) ? 1 : 2;
  if (pixels * bpp > levelSize)
  {
    return false;
  }
  for (size_t i = 0; i < pixels; i++)
  {
    unsigned char* o = &t.RGBA[i * 4];
    const unsigned char* s = data + i * bpp;
    const unsigned v = bpp == 2 ? (s[0] | (s[1] << 8)) : 0;
    switch (format)
    {
      case 0x0500: o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = s[3]; break; /* 8888, BGRA */
      case 0x0600: o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; break;              /* 888, BGRX */
      case 0x0400: o[0] = o[1] = o[2] = s[0]; break;                          /* LUM8 */
      case 0x0100: /* 1555 */
      case 0x0A00: /* 555 */
        o[0] = static_cast<unsigned char>(((v >> 10) & 0x1F) * 255 / 31);
        o[1] = static_cast<unsigned char>(((v >> 5) & 0x1F) * 255 / 31);
        o[2] = static_cast<unsigned char>((v & 0x1F) * 255 / 31);
        if (format == 0x0100)
        {
          o[3] = (v & 0x8000) ? 255 : 0;
        }
        break;
      case 0x0200: Decode565(v, o); break;
      case 0x0300: /* 4444 */
        o[0] = static_cast<unsigned char>(((v >> 8) & 0x0F) * 17);
        o[1] = static_cast<unsigned char>(((v >> 4) & 0x0F) * 17);
        o[2] = static_cast<unsigned char>((v & 0x0F) * 17);
        o[3] = static_cast<unsigned char>(((v >> 12) & 0x0F) * 17);
        break;
      default: return false;
    }
  }
  return true;
}

void ReadTextureDictionary(const std::string& b, std::map<std::string, std::unique_ptr<Texture>>& out)
{
  if (b.size() < 12 || Rd32(b, 0) != 0x16)
  {
    return;
  }
  const size_t end = std::min<size_t>(b.size(), 12 + static_cast<size_t>(Rd32(b, 4)));
  size_t p = 12;
  while (p + 12 <= end)
  {
    const DWORD type = Rd32(b, p), size = Rd32(b, p + 4);
    const size_t data = p + 12, next = data + size;
    if (next > end)
    {
      break;
    }
    if (type == 0x15 && size >= 12 && Rd32(b, data) == 0x01)
    {
      const size_t structEnd = std::min(next, data + 12 + static_cast<size_t>(Rd32(b, data + 4)));
      std::string name;
      std::unique_ptr<Texture> t(new Texture);
      if (ReadNativeTexture(b, data + 12, structEnd, name, *t) && !out.count(name))
      {
        out[name] = std::move(t);
      }
    }
    p = next;
  }
}

/* ---------- the systems of the game ---------- */

std::map<std::string, std::unique_ptr<FxSystem>> g_systems;
std::map<std::string, std::unique_ptr<Texture>> g_textures;
bool g_loaded, g_load_ok;

bool LoadProject()
{
  if (g_loaded)
  {
    return g_load_ok;
  }
  g_loaded = true;
  const char* game = *reinterpret_cast<const char* const*>(GAME_DIR);
  if (!game)
  {
    return false;
  }
  std::string text, txd;
  if (!ReadWholeFile(std::string(game) + "\\models\\effects.fxp", text))
  {
    return false;
  }
  g_systems = ParseFxProject(text);
  if (ReadWholeFile(std::string(game) + "\\models\\effectsPC.txd", txd))
  {
    ReadTextureDictionary(txd, g_textures);
  }
  for (auto& system : g_systems)
  {
    for (FxEmitter& emitter : system.second->Emitters)
    {
      for (const std::string& name : emitter.TextureNames)
      {
        auto t = g_textures.find(name);
        emitter.Textures.push_back(t == g_textures.end() ? nullptr : t->second.get());
      }
    }
  }
  g_load_ok = !g_systems.empty();
  return g_load_ok;
}

/* The OpenGL texture of a texture, with its smaller levels (box filtered) so far sprites don't shimmer */
GLuint TextureName(Texture* t)
{
  if (t->Name || t->RGBA.empty())
  {
    return t->Name;
  }
  g_gl.GenTextures(1, &t->Name);
  g_gl.BindTexture(GL_TEXTURE_2D, t->Name);
  g_gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
  std::vector<unsigned char> level = t->RGBA, smaller;
  int w = t->Width, h = t->Height, n = 0;
  const bool pow2 = !(w & (w - 1)) && !(h & (h - 1));
  for (;;)
  {
    g_gl.TexImage2D(GL_TEXTURE_2D, n, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, level.data());
    if (!pow2 || (w == 1 && h == 1))
    {
      break;
    }
    const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
    smaller.assign(static_cast<size_t>(nw) * nh * 4, 0);
    for (int y = 0; y < nh; y++)
    {
      for (int x = 0; x < nw; x++)
      {
        for (int c = 0; c < 4; c++)
        {
          const int x0 = std::min(x * 2, w - 1), x1 = std::min(x * 2 + 1, w - 1);
          const int y0 = std::min(y * 2, h - 1), y1 = std::min(y * 2 + 1, h - 1);
          const int sum = level[(y0 * w + x0) * 4 + c] + level[(y0 * w + x1) * 4 + c] +
            level[(y1 * w + x0) * 4 + c] + level[(y1 * w + x1) * 4 + c];
          smaller[(static_cast<size_t>(y) * nw + x) * 4 + c] = static_cast<unsigned char>((sum + 2) / 4);
        }
      }
    }
    level.swap(smaller);
    w = nw;
    h = nh;
    n++;
  }
  g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, n);
  g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, n > 0 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  g_gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return t->Name;
}

/* ---------- a system playing on an object (Particles.cpp of model-preview) ---------- */

/* A point or a direction of an emitter in its system (FxPrimBP_c::GetRWMatrix) */
Vec3 Transform(const FxEmitter& e, Vec3 v, bool point)
{
  if (!e.HasMatrix)
  {
    return v;
  }
  const float* m = e.Matrix;
  return { v.X * m[0] + v.Y * m[3] + v.Z * m[6] + (point ? m[9] : 0.0f),
    v.X * m[1] + v.Y * m[4] + v.Z * m[7] + (point ? m[10] : 0.0f),
    v.X * m[2] + v.Y * m[5] + v.Z * m[8] + (point ? m[11] : 0.0f) };
}

/* the values of an info at a time, the defaults when the emitter has none */
template <size_t N>
void Values(const FxEmitter& e, int type, float time, float (&values)[N], std::initializer_list<float> defaults)
{
  const FxInfo& info = e.Info(type);
  if (info.Present)
  {
    info.At(time, values, N);
  }
  else
  {
    std::copy(defaults.begin(), defaults.end(), values);
  }
}

/* particles emitted over a span of the system time (FxInfoEmRate_c); 10 a second without one */
float Emitted(const FxEmitter& e, float from, float to, float length)
{
  const FxInfo& info = e.Info(EmRate);
  if (!info.Present || info.Channels.empty())
  {
    return (to - from) * 10.0f;
  }
  const FxChannel& channel = info.Channels[0];
  auto sum = [&](float a, float b)
  {
    const int steps = std::max(1, static_cast<int>(std::ceil((b - a) / 0.01f)));
    float total = 0;
    for (int s = 0; s < steps; s++)
    {
      total += channel.At(a + (b - a) * (s + 0.5f) / steps);
    }
    return total * (b - a) / steps;
  };
  return from >= 0 ? sum(from, to) : sum(length + from, length) + sum(0, to);
}

struct Quad
{
  Vec3 Corners[4];
  Vec3 Center;
  float Color[4];
  Texture* Tex;
  GLenum Source, Destination;
  bool Additive;
  float Depth;
};

class Effect
{
public:
  Effect(const FxSystem* system, DWORD seed) : System(system), Seed(seed ? seed : 1)
  {
    for (const FxEmitter& emitter : system->Emitters)
    {
      Emitters.push_back({ &emitter, 0.0f, {} });
    }
  }

  /* where the object puts the system: its 2dEffect point in the world, the turn of the object */
  Vec3 Origin;
  Mat3 Turn;

  float Length() const { return System->Length > 0 ? System->Length : 1.0f; }
  bool PlaysOnce() const { return System->PlayMode == 0; }

  /* a system that keeps playing is run a while first, as if it had been burning all along */
  void Start()
  {
    if (!PlaysOnce())
    {
      Advance(2.0f);
    }
  }

  void Advance(float seconds)
  {
    if (seconds <= 0)
    {
      return;
    }
    const int steps = std::max(1, static_cast<int>(std::ceil(seconds / STEP - 1e-4f)));
    for (int s = 0; s < steps; s++)
    {
      Step(seconds / steps);
    }
  }

  void AddQuads(Vec3 eye, Vec3 right, Vec3 up, std::vector<Quad>& quads) const;

private:
  struct Particle
  {
    Vec3 Position, Velocity; /* in the world */
    float Life = 0, Total = 1, Rotation = 0;
    float Randoms[3] = {};
  };
  struct Emitter
  {
    const FxEmitter* Blueprint;
    float Intensity;
    std::vector<Particle> Particles;
  };

  float Random()
  {
    /* mulberry32 */
    Seed += 0x6D2B79F5u;
    DWORD t = Seed;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<float>((t ^ (t >> 14)) >> 8) / 16777216.0f;
  }

  Particle Create(const Emitter& emitter);
  bool Update(const Emitter& emitter, Particle& particle, float dt);
  void Step(float dt);

  const FxSystem* System;
  float Time = 0;
  DWORD Seed;
  std::vector<Emitter> Emitters;
};

/* a particle as FxEmitter_c::CreateParticle makes it, then put in the world by the object */
Effect::Particle Effect::Create(const Emitter& emitter)
{
  const FxEmitter& bp = *emitter.Blueprint;
  const float time = Time;
  float life[2], rotation[2], size[7], speed[2], direction[3], angle[2], start[3];
  Values(bp, EmLife, time, life, { 1, 0 });
  Values(bp, EmRotation, time, rotation, { 0, 0 });
  Values(bp, EmSize, time, size, { 0, 0, 0, 0, 0, 0, 0 });
  Values(bp, EmSpeed, time, speed, { 0, 0 });
  Values(bp, EmDir, time, direction, { 0, 0, 1 });
  Values(bp, EmAngle, time, angle, { 0, 0 });
  Values(bp, EmPos, time, start, { 0, 0, 0 });

  Particle particle;
  Vec3 offset;
  if (std::abs(size[0]) < 0.001f)
  {
    offset = { size[1] + Random() * (size[2] - size[1]), size[3] + Random() * (size[4] - size[3]),
      size[5] + Random() * (size[6] - size[5]) };
  }
  else
  {
    const Vec3 v = SafeNormalize(Vec3{ Random() * 2 - 1, Random() * 2 - 1, Random() * 2 - 1 }, Vec3{ 0, 0, 1 });
    offset = v * (size[0] < 0 ? size[0] : Random() * size[0]);
  }
  const Vec3 local = Transform(bp, offset + Vec3{ start[0], start[1], start[2] }, true);

  /* a direction in the cone of the emission angles around the direction of the emitter (the
     position from the centre when its X is over 10), turned into it as RotateVecIntoVec does */
  const float around = Random() * 2 * PI;
  const float tilt = (angle[0] + (angle[1] - angle[0]) * Random()) * PI / 180.0f;
  const Vec3 cone{ std::cos(around) * std::sin(tilt), std::cos(tilt), std::sin(around) * std::sin(tilt) };
  const Vec3 axis = direction[0] > 10 ? local
                                      : SafeNormalize(Vec3{ direction[0], direction[1], direction[2] }, Vec3{ 0, 0, 1 });
  const Vec3 up = SafeNormalize(Transform(bp, axis, false), Vec3{ 0, 0, 1 });
  const Vec3 right = SafeNormalize(Cross(up, SafeNormalize(Vec3{ 3, 4, 5 }, Vec3{})), Vec3{ 1, 0, 0 });
  const Vec3 at = Cross(up, right);
  const float s = (Random() * 2 - 1) * speed[1] + speed[0];
  particle.Position = Origin + Turn * local;
  particle.Velocity = Turn * ((right * cone.X + up * cone.Y + at * cone.Z) * s);
  particle.Total = (Random() * 2 - 1) * life[1] + life[0];
  particle.Rotation = rotation[0] + Random() * (rotation[1] - rotation[0]);
  for (float& r : particle.Randoms)
  {
    r = Random();
  }
  return particle;
}

/* a particle a step on (FxEmitterBP_c::UpdateParticle); false once its life is over */
bool Effect::Update(const Emitter& emitter, Particle& particle, float dt)
{
  particle.Life += dt;
  if (particle.Life >= particle.Total)
  {
    return false;
  }
  const FxEmitter& bp = *emitter.Blueprint;
  const float age = particle.Life / particle.Total;
  const float systemAge = Time / Length();
  Vec3& v = particle.Velocity;
  Vec3& p = particle.Position;
  static const Vec3 WIND_DIRECTION{ 0.8f, 0.6f, 0.0f };
  for (int type : bp.Movement)
  {
    const FxInfo& info = bp.Info(type);
    float values[4];
    info.At(info.ParticleTime ? age : systemAge, values, 4);
    switch (type)
    {
      case Force:
        v = v + Vec3{ values[0], values[1], values[2] } * dt;
        break;
      case Friction:
        v = v * std::pow(values[0], dt * 50.0f);
        break;
      case Wind:
        v = v + WIND_DIRECTION * (values[0] * WIND_SPEED * dt);
        break;
      case Noise:
      case Jitter:
      {
        /* the game makes its random vector of one number: along the diagonal, one way or the other */
        const float push = (Random() < 0.5f ? -1.0f : 1.0f) * values[0] * dt / std::sqrt(3.0f);
        if (type == Jitter)
        {
          p = p + Vec3{ push, push, push };
        }
        else
        {
          const float before = Len(v);
          v = v + Vec3{ push, push, push };
          const float after = Len(v);
          if (after > 0)
          {
            v = v * (before / after);
          }
        }
        break;
      }
      case AttractPt:
      {
        const Vec3 to = Origin + Turn * Vec3{ values[0], values[1], values[2] } - p;
        if (Len(to) > 0.1f)
        {
          v = v + SafeNormalize(to, Vec3{}) * (values[3] * dt);
        }
        break;
      }
      default:
        break;
    }
  }
  p = p + v * dt;
  /* spinning one way or the other at a speed between the least and the most of that way */
  const FxInfo& spin = bp.Info(RotSpeed);
  if (spin.Present)
  {
    float speeds[4];
    spin.At(spin.ParticleTime ? age : systemAge, speeds, 4);
    const float r = particle.Randoms[2];
    particle.Rotation += (r < 0.5f ? speeds[0] + (speeds[1] - speeds[0]) * r * 2
                                   : -(speeds[2] + (speeds[3] - speeds[2]) * (r - 0.5f) * 2)) * dt;
  }
  return true;
}

/* The system a step on: the particles age and move, each emitter adds whole particles as its rate
   says. One that keeps playing wraps its time at its length; one that plays once stops making
   particles at its length and plays again once its last particle is gone (camflash flickers,
   explosions come again and again). */
void Effect::Step(float dt)
{
  const float length = Length();
  const bool once = PlaysOnce();
  const float from = Time;
  float time = from + dt;
  if (once)
  {
    time = std::min(time, length);
  }
  else if (time > length)
  {
    time -= length * std::floor(time / length);
  }
  Time = time;
  bool any = false;
  for (Emitter& emitter : Emitters)
  {
    std::vector<Particle>& particles = emitter.Particles;
    particles.erase(std::remove_if(particles.begin(), particles.end(),
                      [&](Particle& particle) { return !Update(emitter, particle, dt); }),
      particles.end());
    if (!emitter.Blueprint->Info(HeatHaze).Present && !(once && from >= length))
    {
      emitter.Intensity += once ? Emitted(*emitter.Blueprint, from, time, length)
                                : Emitted(*emitter.Blueprint, time - dt, time, length);
      if (emitter.Intensity >= 1)
      {
        const int count = std::min(static_cast<int>(emitter.Intensity), 400);
        for (int k = 0; k < count && particles.size() < 2000; k++)
        {
          Particle particle = Create(emitter);
          if (Update(emitter, particle, dt * k / emitter.Intensity))
          {
            particles.push_back(particle);
          }
        }
        emitter.Intensity = 0;
      }
    }
    any = any || !particles.empty();
  }
  if (once && time >= length && !any)
  {
    Time = 0;
  }
}

/* the look of each particle (FxEmitterBP_c::Render): colour, size, sprite rectangle, texture, turn */
void Effect::AddQuads(Vec3 eye, Vec3 right, Vec3 up, std::vector<Quad>& quads) const
{
  static const GLenum BLEND[11] = { GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA,
    GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
    GL_SRC_ALPHA_SATURATE };
  const float systemAge = Time / Length();
  for (const Emitter& emitter : Emitters)
  {
    const FxEmitter& bp = *emitter.Blueprint;
    if (bp.Textures.empty())
    {
      continue;
    }
    const GLenum source = BLEND[std::min(std::max(bp.SourceBlend, 0), 10)];
    const GLenum destination = BLEND[std::min(std::max(bp.DestinationBlend, 0), 10)];
    for (const Particle& particle : emitter.Particles)
    {
      const float age = particle.Life / particle.Total;
      auto value = [&](int type, float* values, size_t count)
      {
        const FxInfo& info = bp.Info(type);
        if (!info.Present)
        {
          return false;
        }
        info.At(info.ParticleTime ? age : systemAge, values, count);
        return true;
      };
      float color[5] = { 255, 255, 255, 255, 0 };
      value(Colour, color, 4);
      if (value(ColourBright, color, 5))
      {
        const float bias = (particle.Randoms[0] * 2 - 1) * color[4];
        for (int c = 0; c < 3; c++)
        {
          color[c] += bias;
        }
      }
      const float alpha = std::min(std::max(color[3] / 255.0f, 0.0f), 1.0f);
      if (alpha <= 0.002f)
      {
        continue;
      }
      float size[4] = { 0.1f, 0.1f, 0, 0 };
      value(Size, size, 4);
      const float r = particle.Randoms[1] * 2 - 1;
      const float width = size[0] + r * size[2], height = size[1] + r * size[3];
      float rect[4] = { 0.5f, -0.5f, 0.5f, -0.5f }; /* top, bottom, left, right */
      value(SpriteRect, rect, 4);
      float textureIndex = 0;
      value(AnimTex, &textureIndex, 1);
      const size_t index = std::min(static_cast<size_t>(std::max(0.0f, textureIndex)), bp.Textures.size() - 1);
      Texture* texture = bp.Textures[index];
      if (!texture)
      {
        continue;
      }

      Quad quad;
      quad.Tex = texture;
      quad.Source = source;
      quad.Destination = destination;
      quad.Additive = destination == GL_ONE;
      for (int c = 0; c < 3; c++)
      {
        quad.Color[c] = std::min(std::max(color[c] / 255.0f, 0.0f), 1.0f);
      }
      quad.Color[3] = alpha;
      const Vec3 center = particle.Position;
      quad.Center = center;

      /* the axes of the sprite: facing the camera; along a direction (the velocity when the
         direction is none), facing the camera round it; or flat in the plane of the system */
      Vec3 x = right, y = up;
      float turn = particle.Rotation;
      float flat[9], dir[3];
      if (value(Flat, flat, 9))
      {
        x = Turn * Vec3{ flat[0], flat[1], flat[2] };
        y = Turn * Vec3{ flat[3], flat[4], flat[5] };
      }
      else if (value(Dir, dir, 3))
      {
        const Vec3 d = Len(Vec3{ dir[0], dir[1], dir[2] }) < 0.001f ? particle.Velocity
                                                                     : Turn * Vec3{ dir[0], dir[1], dir[2] };
        const Vec3 along = SafeNormalize(d, Vec3{});
        const Vec3 across = SafeNormalize(Cross(center - eye, along), Vec3{});
        if (Len(along) > 0 && Len(across) > 0)
        {
          y = along;
          x = across;
          turn = 0;
        }
      }
      if (turn != 0)
      {
        const float a = turn * PI / 180.0f;
        const Vec3 turned = x * std::cos(a) + y * std::sin(a);
        y = y * std::cos(a) - x * std::sin(a);
        x = turned;
      }
      const float corners[4][2] = { { rect[2], rect[0] }, { rect[3], rect[0] }, { rect[3], rect[1] }, { rect[2], rect[1] } };
      for (int k = 0; k < 4; k++)
      {
        quad.Corners[k] = center + x * (corners[k][0] * width) + y * (corners[k][1] * height);
      }
      quads.push_back(quad);
    }
  }
}

/* ---------- the effect models and the objects of the map ---------- */

struct EffectModel
{
  std::string Name;     /* lower case, without extension */
  std::string Particle; /* lower case */
  Vec3 Offset;
};

std::vector<EffectModel> g_models;
int g_models_generation = 1;
short g_model_of_id[IDE_MAX_ID + 1];  /* index in g_models, -1 none */
int g_resolved_generation[IDE_MAX_ID + 1];

/* the effect model of an IDE id, once the editor has read its DFF */
int ModelOfId(int id)
{
  if (id < 0 || id > IDE_MAX_ID)
  {
    return -1;
  }
  if (g_resolved_generation[id] == g_models_generation)
  {
    return g_model_of_id[id];
  }
  g_resolved_generation[id] = g_models_generation;
  g_model_of_id[id] = -1;
  const BYTE* def = reinterpret_cast<const BYTE* const*>(IDE_BY_ID)[id];
  const char* name = def ? *reinterpret_cast<const char* const*>(def + 8) : nullptr;
  if (!name)
  {
    return -1;
  }
  const std::string n = Lower(name);
  for (size_t i = 0; i < g_models.size(); i++)
  {
    if (g_models[i].Name == n)
    {
      g_model_of_id[id] = static_cast<short>(i);
      break;
    }
  }
  return g_model_of_id[id];
}

struct Placed
{
  BYTE* Instance;
  DWORD Name; /* its selection name: IPL index | instance index << 16 */
  int Model;
  Vec3 Position;
  float Quat[4];
  float Distance;
};

/* the effect objects of the map within the draw distance of the eye */
bool CollectPlaced(Vec3 eye, std::vector<Placed>& out)
{
  __try
  {
    BYTE* map = *reinterpret_cast<BYTE**>(MAP_PTR);
    if (!map)
    {
      return true;
    }
    BYTE** ipls = *reinterpret_cast<BYTE***>(map + 0x3C);
    const int iplCount = ipls ? reinterpret_cast<int*>(ipls)[-1] : 0;
    for (int i = 0; i < iplCount; i++)
    {
      BYTE** insts = *reinterpret_cast<BYTE***>(ipls[i] + 8);
      const int count = insts ? reinterpret_cast<int*>(insts)[-1] : 0;
      for (int j = 0; j < count; j++)
      {
        BYTE* o = insts[j];
        if (o[INST_GONE])
        {
          continue;
        }
        const int model = ModelOfId(*reinterpret_cast<int*>(o + INST_MODEL));
        if (model < 0)
        {
          continue;
        }
        const float* pos = reinterpret_cast<const float*>(o + INST_POS);
        Placed p;
        p.Instance = o;
        p.Name = (static_cast<DWORD>(i) & 0xFFFF) | (static_cast<DWORD>(j) << 16);
        p.Model = model;
        p.Position = { pos[0], pos[1], pos[2] };
        std::memcpy(p.Quat, o + INST_QUAT, sizeof p.Quat);
        p.Distance = Len(p.Position - eye);
        if (p.Distance <= DRAW_DISTANCE)
        {
          out.push_back(p);
        }
      }
    }
    return true;
  }
  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    return false;
  }
}

/* The turn of an instance as the editor draws it (TForm1 draws an instance at 0x4E11E8): an axis and
   angle from its quaternion, the matrix of the editor's own function */
Mat3 TurnOf(const float q[4])
{
  Mat3 turn;
  const float w = -q[3];
  const float s = std::sqrt(std::max(0.0f, 1.0f - w * w));
  if (s == 0)
  {
    return turn;
  }
  float angle = 2.0f * std::atan2(s, w);
  if (angle == 0)
  {
    return turn;
  }
  angle = angle * 180.0f / PI;
  float qx = q[0] / s, qy = q[1] / s, qz = q[2] / s;
  float m[16];
  DWORD fn = FN_AXIS_MATRIX;
  float* dst = m;
  __asm {
    push angle
    push qx
    push qy
    push qz
    mov eax, dst
    call fn
  }
  turn.M[0] = m[0]; turn.M[1] = m[1]; turn.M[2] = m[2];
  turn.M[3] = m[4]; turn.M[4] = m[5]; turn.M[5] = m[6];
  turn.M[6] = m[8]; turn.M[7] = m[9]; turn.M[8] = m[10];
  return turn;
}

struct Live
{
  int Model = -1;
  std::unique_ptr<Effect> Fx;
  DWORD Frame = 0;
};

std::unordered_map<BYTE*, Live> g_live;
DWORD g_frame;
LARGE_INTEGER g_last_time, g_frequency;

float g_view[16];
bool g_have_view;

/* the camera of the captured view: eye, right and up in the world */
void CameraOf(const float* m, Vec3& eye, Vec3& right, Vec3& up, Vec3& forward)
{
  right = { m[0], m[4], m[8] };
  up = { m[1], m[5], m[9] };
  forward = { -m[2], -m[6], -m[10] };
  const Vec3 t{ m[12], m[13], m[14] };
  eye = { -(m[0] * t.X + m[1] * t.Y + m[2] * t.Z), -(m[4] * t.X + m[5] * t.Y + m[6] * t.Z),
    -(m[8] * t.X + m[9] * t.Y + m[10] * t.Z) };
}

void Vertex(Vec3 p) { g_gl.Vertex3f(p.X, p.Y, p.Z); }

/* The editor (Delphi) runs the x87 with invalid operation, division by zero and overflow unmasked: a
   NaN of the particles would raise an exception in it. They are masked while the particles run, and
   the editor's control word comes back after, without the flags they left. */
struct FpuGuard
{
  unsigned int Old = 0;
  FpuGuard()
  {
    __control87_2(0, 0, &Old, nullptr);
    unsigned int now;
    __control87_2(_MCW_EM, _MCW_EM, &now, nullptr);
  }
  ~FpuGuard()
  {
    unsigned int now;
    _clearfp();
    __control87_2(Old, 0xFFFFFFFF, &now, nullptr);
  }
};

void DrawScene(std::vector<Quad>& quads, Vec3 eye, Vec3 forward)
{
  for (Quad& q : quads)
  {
    q.Depth = Dot(q.Center - eye, forward);
  }
  std::stable_sort(quads.begin(), quads.end(), [](const Quad& a, const Quad& b)
    {
      if (a.Additive != b.Additive)
      {
        return !a.Additive;
      }
      return !a.Additive && a.Depth > b.Depth;
    });

  g_gl.PushAttrib(GL_ALL_ATTRIB_BITS);
  g_gl.Disable(GL_LIGHTING);
  g_gl.Disable(GL_CULL_FACE);
  g_gl.Disable(GL_ALPHA_TEST);
  g_gl.Disable(GL_COLOR_MATERIAL);
  g_gl.Disable(GL_TEXTURE_GEN_S);
  g_gl.Disable(GL_TEXTURE_GEN_T);
  g_gl.Disable(GL_TEXTURE_GEN_R);
  g_gl.Disable(GL_TEXTURE_GEN_Q);
  g_gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  g_gl.Enable(GL_DEPTH_TEST);
  g_gl.DepthMask(0);
  g_gl.Enable(GL_BLEND);
  g_gl.Enable(GL_TEXTURE_2D);
  g_gl.TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
  g_gl.MatrixMode(GL_TEXTURE);
  g_gl.PushMatrix();
  g_gl.LoadIdentity();

  static const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
  for (size_t start = 0; start < quads.size();)
  {
    const Quad& first = quads[start];
    size_t end = start;
    while (end < quads.size() && quads[end].Tex == first.Tex && quads[end].Source == first.Source &&
      quads[end].Destination == first.Destination)
    {
      end++;
    }
    const GLuint name = TextureName(first.Tex);
    if (name)
    {
      g_gl.BindTexture(GL_TEXTURE_2D, name);
      g_gl.BlendFunc(first.Source, first.Destination);
      g_gl.Begin(GL_QUADS);
      for (size_t i = start; i < end; i++)
      {
        const Quad& q = quads[i];
        g_gl.Color4f(q.Color[0], q.Color[1], q.Color[2], q.Color[3]);
        for (int k = 0; k < 4; k++)
        {
          g_gl.TexCoord2f(uv[k][0], uv[k][1]);
          Vertex(q.Corners[k]);
        }
      }
      g_gl.End();
    }
    start = end;
  }

  g_gl.PopMatrix();
  g_gl.MatrixMode(GL_MODELVIEW);
  g_gl.PopAttrib();
}
}

/* ---------- entry points ---------- */

extern "C" void fx_note_model(const char* dffname, const char* particle, const float offset[3])
{
  if (!dffname || !particle || !particle[0])
  {
    return;
  }
  std::string name = Lower(std::string(dffname, strnlen(dffname, 24)));
  const size_t dot = name.find('.');
  if (dot != std::string::npos)
  {
    name.resize(dot);
  }
  for (const EffectModel& m : g_models)
  {
    if (m.Name == name)
    {
      return;
    }
  }
  if (g_models.size() >= 0x7FFF)
  {
    return;
  }
  EffectModel m;
  m.Name = name;
  m.Particle = Lower(std::string(particle, strnlen(particle, 24)));
  m.Offset = { offset[0], offset[1], offset[2] };
  g_models.push_back(m);
  g_models_generation++;
}

/* gluLookAt of the editor's scene, the view kept for the particles */
extern "C" void __stdcall fx_look_at(double ex, double ey, double ez, double cx, double cy, double cz,
  double ux, double uy, double uz)
{
  typedef void(WINAPI * LookAt)(double, double, double, double, double, double, double, double, double);
  LookAt real = *reinterpret_cast<LookAt*>(*reinterpret_cast<DWORD*>(GLU_LOOKAT));
  real(ex, ey, ez, cx, cy, cz, ux, uy, uz);
  if (LoadGl())
  {
    g_gl.GetFloatv(GL_MODELVIEW_MATRIX, g_view);
    g_have_view = true;
  }
}

/* what the patched "mov eax,[slot]; mov eax,[eax]; call eax" of the editor finds in place of its slot */
extern "C" void* fx_look_at_slot = reinterpret_cast<void*>(&fx_look_at);

static void AfterScene();

extern "C" void fx_after_scene(void)
{
  FpuGuard fpu;
  AfterScene();
}

static void AfterScene()
{
  if (!g_have_view || !LoadGl() || !LoadProject() || g_models.empty())
  {
    return;
  }
  Vec3 eye, right, up, forward;
  CameraOf(g_view, eye, right, up, forward);

  std::vector<Placed> placed;
  if (!CollectPlaced(eye, placed))
  {
    return;
  }
  if (placed.size() > MAX_EFFECTS)
  {
    std::nth_element(placed.begin(), placed.begin() + MAX_EFFECTS, placed.end(),
      [](const Placed& a, const Placed& b) { return a.Distance < b.Distance; });
    placed.resize(MAX_EFFECTS);
  }

  GLint mode = 0;
  g_gl.GetIntegerv(GL_RENDER_MODE, &mode);
  const bool picking = mode == GL_SELECT;

  float dt = 0;
  if (!picking)
  {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!g_frequency.QuadPart)
    {
      QueryPerformanceFrequency(&g_frequency);
    }
    if (g_last_time.QuadPart)
    {
      dt = static_cast<float>(static_cast<double>(now.QuadPart - g_last_time.QuadPart) / g_frequency.QuadPart);
    }
    g_last_time = now;
    dt = std::min(std::max(dt, 0.0f), 0.25f);
    g_frame++;
  }

  std::vector<Quad> quads;
  std::vector<std::pair<size_t, DWORD>> owners; /* first quad of each object, its selection name */
  for (const Placed& p : placed)
  {
    const EffectModel& model = g_models[p.Model];
    auto system = g_systems.find(model.Particle);
    if (system == g_systems.end())
    {
      continue;
    }
    Live& live = g_live[p.Instance];
    const Mat3 turn = TurnOf(p.Quat);
    const Vec3 origin = p.Position + turn * model.Offset;
    if (live.Model != p.Model || !live.Fx)
    {
      live.Model = p.Model;
      live.Fx.reset(new Effect(system->second.get(), static_cast<DWORD>(reinterpret_cast<UINT_PTR>(p.Instance)) * 2654435761u));
      live.Fx->Origin = origin;
      live.Fx->Turn = turn;
      live.Fx->Start();
    }
    live.Fx->Origin = origin;
    live.Fx->Turn = turn;
    if (!picking)
    {
      live.Frame = g_frame;
      live.Fx->Advance(dt);
    }
    owners.push_back({ quads.size(), p.Name });
    live.Fx->AddQuads(eye, right, up, quads);
  }
  if (!picking)
  {
    /* the effects no longer seen stop playing */
    for (auto it = g_live.begin(); it != g_live.end();)
    {
      it = it->second.Frame == g_frame ? std::next(it) : g_live.erase(it);
    }
  }

  g_gl.MatrixMode(GL_MODELVIEW);
  g_gl.PushMatrix();
  g_gl.LoadMatrixf(g_view);
  if (!picking)
  {
    if (!quads.empty())
    {
      DrawScene(quads, eye, forward);
    }
  }
  else
  {
    /* A click selects the object of the effect: its core (a sprite of 60 cm at the point of the
       effect) and its particles that show (up to 2 m each: a cloud of smoke does not hide what is
       behind it from the clicks) */
    for (size_t o = 0; o < owners.size(); o++)
    {
      const size_t from = owners[o].first;
      const size_t to = o + 1 < owners.size() ? owners[o + 1].first : quads.size();
      const Placed* p = nullptr;
      for (const Placed& q : placed)
      {
        if (q.Name == owners[o].second)
        {
          p = &q;
          break;
        }
      }
      g_gl.PushName(owners[o].second);
      g_gl.Begin(GL_QUADS);
      if (p)
      {
        const Mat3 turn = TurnOf(p->Quat);
        const Vec3 c = p->Position + turn * g_models[p->Model].Offset;
        const Vec3 x = right * 0.3f, y = up * 0.3f;
        Vertex(c - x + y);
        Vertex(c + x + y);
        Vertex(c + x - y);
        Vertex(c - x - y);
      }
      for (size_t i = from; i < to; i++)
      {
        const Quad& q = quads[i];
        if (q.Color[3] < 0.25f)
        {
          continue;
        }
        const float side = std::max(Len(q.Corners[1] - q.Corners[0]), Len(q.Corners[3] - q.Corners[0]));
        const float k = side > 2.0f ? 2.0f / side : 1.0f;
        for (int n = 0; n < 4; n++)
        {
          Vertex(q.Center + (q.Corners[n] - q.Center) * k);
        }
      }
      g_gl.End();
      g_gl.PopName();
    }
  }
  g_gl.MatrixMode(GL_MODELVIEW);
  g_gl.PopMatrix();
}
