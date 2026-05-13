#include "Render.h"
#include "GUItextRectangle.h"
#include "MyShaders.h"
#include "ObjLoader.h"
#include "Texture.h"

#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <iomanip>
#include <iostream>
#include <sstream>


#include "debout.h"

// Внутренняя логика "движка"
#include "MyOGL.h"
extern OpenGL gl;
#include "Light.h"
Light light;
#include "Camera.h"
Camera camera;

bool texturing = true;
bool lightning = true;
bool alpha = false;

bool mateAnimation = false;
int mateStage = 0;
double stageTimer = 0;
void ResetPieces();

struct ChessPiece
{
    ObjModel* model;

    double x;
    double y;
    double z = 0.569;

    double startX;
    double startY;

    double targetX;
    double targetY;

    bool moving = false;

    bool flip = false;

    bool alive = true;

    // Для анимации сальто
    bool trickActive = false;
    double trickT = 0.0;
    double trickDuration = 2.0;
    double rotationAngle = 0.0;
};

bool kingTrickAnimation = false;
ChessPiece* activeKing = nullptr;
void StartKingTrick();

ChessPiece EmRook1, EmRook2, EmKnight1, EmKnight2, EmBishop1, EmBishop2, EmQueen, EmKing, EmPawn1, EmPawn2, EmPawn3, EmPawn4, EmPawn5, EmPawn6, EmPawn7, EmPawn8;
ChessPiece DiRook1, DiRook2, DiKnight1, DiKnight2, DiBishop1, DiBishop2, DiQueen, DiKing, DiPawn1, DiPawn2, DiPawn3, DiPawn4, DiPawn5, DiPawn6, DiPawn7, DiPawn8;

// Переключение режимов освещения, текстурирования, альфа-наложения, а также запуск анимации мата и трюка
void switchModes(OpenGL* sender, KeyEventArg arg)
{
    // Конвертируем код клавиши в букву
    auto key = LOWORD(MapVirtualKeyA(arg.key, MAPVK_VK_TO_CHAR));

    switch (key)
    {
    case 'L':
        lightning = !lightning;
        break;
    case 'T':
        texturing = !texturing;
        break;
    case 'A':
        alpha = !alpha;
        break;
    case 'M':
        if (!mateAnimation)
        {
            ResetPieces();
            mateAnimation = true;
            mateStage = 0;
            stageTimer = 0;
        }
        break;
    case 'K':
        if ((!EmKing.trickActive) && (!DiKing.trickActive))
        {
            StartKingTrick();
        }
        break;
    }
}

// Умножение матриц c[M1][N1] = a[M1][N1] * b[M2][N2]
template <typename T, int M1, int N1, int M2, int N2> void MatrixMultiply(const T* a, const T* b, T* c)
{
    for (int i = 0; i < M1; ++i)
    {
        for (int j = 0; j < N2; ++j)
        {
            c[i * N2 + j] = T(0);
            for (int k = 0; k < N1; ++k)
            {
                c[i * N2 + j] += a[i * N1 + k] * b[k * N2 + j];
            }
        }
    }
}

// Текстовый прямоугольник в верхнем правом углу.
// OGL не предоставляет возможности для хранения текста;
// внутри этого класса создается картинка с текстом (через GDI),
// в виде текстуры накладывается на прямоугольник и рисуется на экране.
// Это самый простой, но очень неэффективный способ написать что-либо на экране.
GuiTextRectangle text;

// ID для текстуры
GLuint texId;

ObjModel table, pawn, rook, knight, bishop, queen, king;

Shader cassini_sh;
Shader phong_sh;
Shader vb_sh;
Shader simple_texture_sh;

Texture Chess_tex, ruby_tex, emerald_tex, diamond_tex;

// Выполняется один раз перед первым рендером
void initRender()
{
    // Настройка шейдеров
    cassini_sh.VshaderFileName = "shaders/v.vert";
    cassini_sh.FshaderFileName = "shaders/cassini.frag";
    cassini_sh.LoadShaderFromFile();
    cassini_sh.Compile();

    phong_sh.VshaderFileName = "shaders/v.vert";
    phong_sh.FshaderFileName = "shaders/light.frag";
    phong_sh.LoadShaderFromFile();
    phong_sh.Compile();

    vb_sh.VshaderFileName = "shaders/v.vert";
    vb_sh.FshaderFileName = "shaders/vb.frag";
    vb_sh.LoadShaderFromFile();
    vb_sh.Compile();

    simple_texture_sh.VshaderFileName = "shaders/v.vert";
    simple_texture_sh.FshaderFileName = "shaders/textureShader.frag";
    simple_texture_sh.LoadShaderFromFile();
    simple_texture_sh.Compile();

    Chess_tex.LoadTexture("textures/Chess_Board.png");
    ruby_tex.LoadTexture("textures/ruby4.jpg");
    emerald_tex.LoadTexture("textures/emerald.jpg");
	diamond_tex.LoadTexture("textures/diamond.jpg");

    table.LoadModel("models//table.obj");
    pawn.LoadModel("models//pawn.obj");
    rook.LoadModel("models//rook.obj");
    knight.LoadModel("models//knight.obj");
    bishop.LoadModel("models//bishop.obj");
    queen.LoadModel("models//queen.obj");
    king.LoadModel("models//king.obj");
    //==============НАСТРОЙКА ТЕКСТУР================
    // 4 байта на хранение пикселя
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    //================НАСТРОЙКА КАМЕРЫ======================
    camera.caclulateCameraPos();

    // привязываем камеру к событиям "движка"
    gl.WheelEvent.reaction(&camera, &Camera::Zoom);
    gl.MouseMovieEvent.reaction(&camera, &Camera::MouseMovie);
    gl.MouseLeaveEvent.reaction(&camera, &Camera::MouseLeave);
    gl.MouseLdownEvent.reaction(&camera, &Camera::MouseStartDrag);
    gl.MouseLupEvent.reaction(&camera, &Camera::MouseStopDrag);
    //==============НАСТРОЙКА СВЕТА===========================
    // Привязываем свет к событиям "движка"
    gl.MouseMovieEvent.reaction(&light, &Light::MoveLight);
    gl.KeyDownEvent.reaction(&light, &Light::StartDrug);
    gl.KeyUpEvent.reaction(&light, &Light::StopDrug);
    //========================================================
    //====================Прочее==============================
    gl.KeyDownEvent.reaction(switchModes);
    text.setSize(700, 260);
    srand((unsigned)time(0));
    //========================================================

    camera.setPosition(0.73, 0.73, 0.77);
    light.SetPosition(0.5, 0.5, 0.8);

    // Параметры фигур
    EmRook1.model = &rook; EmRook1.x = 0.4125; EmRook1.y = 0.4125;
	EmRook2.model = &rook; EmRook2.x = 0.5875; EmRook2.y = 0.4125;
    EmKnight1.model = &knight; EmKnight1.x = 0.4375; EmKnight1.y = 0.4125; EmKnight1.flip = true;
    EmKnight2.model = &knight; EmKnight2.x = 0.5625; EmKnight2.y = 0.4125; EmKnight2.flip = true;
    EmBishop1.model = &bishop; EmBishop1.x = 0.4625; EmBishop1.y = 0.4125;
    EmBishop2.model = &bishop; EmBishop2.x = 0.5375; EmBishop2.y = 0.4125;
    EmQueen.model = &queen; EmQueen.x = 0.4875; EmQueen.y = 0.4125;
    EmKing.model = &king; EmKing.x = 0.5125; EmKing.y = 0.4125;
    EmPawn1.model = &pawn; EmPawn1.x = 0.4125; EmPawn1.y = 0.4375;
    EmPawn2.model = &pawn; EmPawn2.x = 0.4375; EmPawn2.y = 0.4375;
    EmPawn3.model = &pawn; EmPawn3.x = 0.4625; EmPawn3.y = 0.4375;
    EmPawn4.model = &pawn; EmPawn4.x = 0.4875; EmPawn4.y = 0.4375;
    EmPawn5.model = &pawn; EmPawn5.x = 0.5125; EmPawn5.y = 0.4375;
    EmPawn6.model = &pawn; EmPawn6.x = 0.5375; EmPawn6.y = 0.4375;
    EmPawn7.model = &pawn; EmPawn7.x = 0.5625; EmPawn7.y = 0.4375;
    EmPawn8.model = &pawn; EmPawn8.x = 0.5875; EmPawn8.y = 0.4375;

    DiRook1.model = &rook; DiRook1.x = 0.4125; DiRook1.y = 0.5875;
    DiRook2.model = &rook; DiRook2.x = 0.5875; DiRook2.y = 0.5875;
    DiKnight1.model = &knight; DiKnight1.x = 0.4375; DiKnight1.y = 0.5875;
    DiKnight2.model = &knight; DiKnight2.x = 0.5625; DiKnight2.y = 0.5875;
    DiBishop1.model = &bishop; DiBishop1.x = 0.4625; DiBishop1.y = 0.5875; DiBishop1.flip = true;
	DiBishop2.model = &bishop; DiBishop2.x = 0.5375; DiBishop2.y = 0.5875; DiBishop2.flip = true;
    DiQueen.model = &queen; DiQueen.x = 0.4875; DiQueen.y = 0.5875;
    DiKing.model = &king; DiKing.x = 0.5125; DiKing.y = 0.5875;
    DiPawn1.model = &pawn; DiPawn1.x = 0.4125; DiPawn1.y = 0.5625;
    DiPawn2.model = &pawn; DiPawn2.x = 0.4375; DiPawn2.y = 0.5625;
    DiPawn3.model = &pawn; DiPawn3.x = 0.4625; DiPawn3.y = 0.5625;
    DiPawn4.model = &pawn; DiPawn4.x = 0.4875; DiPawn4.y = 0.5625;
    DiPawn5.model = &pawn; DiPawn5.x = 0.5125; DiPawn5.y = 0.5625;
    DiPawn6.model = &pawn; DiPawn6.x = 0.5375; DiPawn6.y = 0.5625;
    DiPawn7.model = &pawn; DiPawn7.x = 0.5625; DiPawn7.y = 0.5625;
    DiPawn8.model = &pawn; DiPawn8.x = 0.5875; DiPawn8.y = 0.5625;
}

float view_matrix[16];
double full_time = 0;
int location = 0;

struct Point
{
    double x, y, z;
};

Point kingPath[4];

Point CubeBezier(Point P[4], double t) {
    Point res;
    res.x = pow(1 - t, 3) * P[0].x + 3 * t * pow(1 - t, 2) * P[1].x + 3 * pow(t, 2) * (1 - t) * P[2].x + pow(t, 3) * P[3].x;
    res.y = pow(1 - t, 3) * P[0].y + 3 * t * pow(1 - t, 2) * P[1].y + 3 * pow(t, 2) * (1 - t) * P[2].y + pow(t, 3) * P[3].y;
    res.z = pow(1 - t, 3) * P[0].z + 3 * t * pow(1 - t, 2) * P[1].z + 3 * pow(t, 2) * (1 - t) * P[2].z + pow(t, 3) * P[3].z;
    return res;
}

Point Normal(double x[][4])
{
    Point p0{ x[0][0], x[0][1], x[0][2] };
    Point p1{ x[1][0], x[1][1], x[1][2] };
    Point p2{ x[2][0], x[2][1], x[2][2] };

    Point v1{ p0.x - p1.x, p0.y - p1.y, p0.z - p1.z };
    Point v2{ p2.x - p1.x, p2.y - p1.y, p2.z - p1.z };

    Point N{
        v1.y * v2.z - v1.z * v2.y,
        v1.z * v2.x - v1.x * v2.z,
        v1.x * v2.y - v1.y * v2.x
    };

    double l = sqrt(N.x * N.x + N.y * N.y + N.z * N.z);
    return { N.x / l, N.y / l, N.z / l };
}

void DrawTexturedQuad(double quad[][4])
{
    glTexCoord2d(0, 0);
    glVertex3dv(quad[0]);

    glTexCoord2d(1, 0);
    glVertex3dv(quad[1]);

    glTexCoord2d(1, 1);
    glVertex3dv(quad[2]);

    glTexCoord2d(0, 1);
    glVertex3dv(quad[3]);
}

void DrawChessBoard()
{
    glPushMatrix();
    glTranslated(0.5, 0.5, 0.559);

    Point normal;
    double chessboard_F_Center[][4]{ {0.1, 0.1, 0}, { 0.1, -0.1, 0 }, { -0.1, -0.1, 0 }, { -0.1, 0.1, 0 } };
    double chessboard_F_1[][4]{ {0.1, 0.1, 0}, { 0.1, -0.1, 0 }, { 0.13, -0.1, 0 }, { 0.13, 0.1, 0 } };
    double chessboard_F_2[][4]{ {0.1, -0.1, 0}, { -0.1, -0.1, 0 }, { -0.1, -0.13, 0 }, { 0.1, -0.13, 0 } };
    double chessboard_F_3[][4]{ {-0.1, -0.1, 0}, { -0.1, 0.1, 0 }, { -0.13, 0.1, 0 }, { -0.13, -0.1, 0 } };
    double chessboard_F_4[][4]{ {-0.1, 0.1, 0}, { 0.1, 0.1, 0 }, { 0.1, 0.13, 0 }, { -0.1, 0.13, 0 } };
    double chessboard_W_1[][4]{ {0.1, 0.1, 0}, { 0.1, -0.1, 0 }, { 0.1, -0.1, 0.01 }, { 0.1, 0.1, 0.01 } };
    double chessboard_W_2[][4]{ { 0.1, -0.1, 0 }, { -0.1, -0.1, 0 }, { -0.1, -0.1, 0.01 }, { 0.1, -0.1, 0.01 } };
    double chessboard_W_3[][4]{ { -0.1, -0.1, 0 }, { -0.1, 0.1, 0 }, { -0.1, 0.1, 0.01 }, { -0.1, -0.1, 0.01 } };
    double chessboard_W_4[][4]{ { -0.1, 0.1, 0 }, { 0.1, 0.1, 0 }, { 0.1, 0.1, 0.01 }, { -0.1, 0.1, 0.01 } };
    double chessboard_C_Center[][4]{ {0.1, 0.1, 0.01}, { 0.1, -0.1, 0.01 }, { -0.1, -0.1, 0.01 }, { -0.1, 0.1, 0.01 } };
    double chessboard_C_1[][4]{ {0.1, 0.1, 0.01}, { 0.1, -0.1, 0.01 }, { 0.12, -0.1, 0.01 }, { 0.12, 0.1, 0.01 } };
    double chessboard_C_2[][4]{ {0.1, -0.1, 0.01}, { -0.1, -0.1, 0.01 }, { -0.1, -0.12, 0.01 }, { 0.1, -0.12, 0.01 } };
    double chessboard_C_3[][4]{ {-0.1, -0.1, 0.01}, { -0.1, 0.1, 0.01 }, { -0.12, 0.1, 0.01 }, { -0.12, -0.1, 0.01 } };
    double chessboard_C_4[][4]{ {-0.1, 0.1, 0.01}, { 0.1, 0.1, 0.01 }, { 0.1, 0.12, 0.01 }, { -0.1, 0.12, 0.01 } };
    double chessboard_corner_1_F[][4]{ {0.1, 0.1, 0}, { 0.13, 0.1, 0 }, { 0.13, 0.13, 0 }, { 0.1, 0.13, 0 } };
    double chessboard_corner_2_F[][4]{ {0.1, -0.1, 0}, { 0.13, -0.1, 0 }, { 0.13, -0.13, 0 }, { 0.1, -0.13, 0 } };
    double chessboard_corner_3_F[][4]{ {-0.1, -0.1, 0}, { -0.13, -0.1, 0 }, { -0.13, -0.13, 0 }, { -0.1, -0.13, 0 } };
    double chessboard_corner_4_F[][4]{ {-0.1, 0.1, 0}, { -0.13, 0.1, 0 }, { -0.13, 0.13, 0 }, { -0.1, 0.13, 0 } };
    double chessboard_corner_1_W1[][4]{ {0.1, 0.1, 0}, { 0.13, 0.1, 0 }, { 0.13, 0.1, 0.025 }, { 0.1, 0.1, 0.025 } };
    double chessboard_corner_1_W2[][4]{ {0.13, 0.1, 0}, { 0.13, 0.13, 0 }, { 0.13, 0.13, 0.025 }, { 0.13, 0.1, 0.025 } };
    double chessboard_corner_1_W3[][4]{ { 0.13, 0.13, 0 }, { 0.1, 0.13, 0 }, { 0.1, 0.13, 0.025 }, { 0.13, 0.13, 0.025 } };
    double chessboard_corner_1_W4[][4]{ {0.1, 0.13, 0}, { 0.1, 0.1, 0 }, { 0.1, 0.1, 0.025 }, { 0.1, 0.13, 0.025 } };
    double chessboard_corner_2_W1[][4]{ {0.1, -0.13, 0}, { 0.13, -0.13, 0 }, { 0.13, -0.13, 0.025 }, { 0.1, -0.13, 0.025 } };
    double chessboard_corner_2_W2[][4]{ {0.13, -0.13, 0}, { 0.13, -0.1, 0 }, { 0.13, -0.1, 0.025 }, { 0.13, -0.13, 0.025 } };
    double chessboard_corner_2_W3[][4]{ {0.1, -0.1, 0}, { 0.13, -0.1, 0 }, { 0.13, -0.1, 0.025 }, { 0.1, -0.1, 0.025 } };
    double chessboard_corner_2_W4[][4]{ {0.1, -0.13, 0}, { 0.1, -0.1, 0 }, { 0.1, -0.1, 0.025 }, { 0.1, -0.13, 0.025 } };
    double chessboard_corner_3_W1[][4]{ { -0.13, -0.13, 0 }, { -0.1, -0.13, 0 }, { -0.1, -0.13, 0.025 }, { -0.13, -0.13, 0.025 } };
    double chessboard_corner_3_W2[][4]{ {-0.1, -0.13, 0}, { -0.1, -0.1, 0 }, { -0.1, -0.1, 0.025 }, { -0.1, -0.13, 0.025 } };
    double chessboard_corner_3_W3[][4]{ {-0.1, -0.1, 0}, { -0.13, -0.1, 0 }, { -0.13, -0.1, 0.025 }, { -0.1, -0.1, 0.025 } };
    double chessboard_corner_3_W4[][4]{ {-0.13, -0.1, 0}, { -0.13, -0.13, 0 }, { -0.13, -0.13, 0.025 }, { -0.13, -0.1, 0.025 } };
    double chessboard_corner_4_W1[][4]{ {-0.13, 0.1, 0}, { -0.1, 0.1, 0 }, { -0.1, 0.1, 0.025 }, { -0.13, 0.1, 0.025 } };
    double chessboard_corner_4_W2[][4]{ {-0.1, 0.1, 0}, { -0.1, 0.13, 0 }, { -0.1, 0.13, 0.025 }, { -0.1, 0.1, 0.025 } };
    double chessboard_corner_4_W3[][4]{ {-0.1, 0.13, 0}, { -0.13, 0.13, 0 }, { -0.13, 0.13, 0.025 }, { -0.1, 0.13, 0.025 } };
    double chessboard_corner_4_W4[][4]{ {-0.13, 0.13, 0}, { -0.13, 0.1, 0 }, { -0.13, 0.1, 0.025 }, { -0.13, 0.13, 0.025 } };
    double chessboard_corner_1_C[][4]{ {0.1, 0.1, 0.025}, { 0.13, 0.1, 0.025 }, { 0.13, 0.13, 0.025 }, { 0.1, 0.13, 0.025 } };
    double chessboard_corner_2_C[][4]{ {0.1, -0.1, 0.025}, { 0.13, -0.1, 0.025 }, { 0.13, -0.13, 0.025 }, { 0.1, -0.13, 0.025 } };
    double chessboard_corner_3_C[][4]{ {-0.1, -0.1, 0.025}, { -0.13, -0.1, 0.025 }, { -0.13, -0.13, 0.025 }, { -0.1, -0.13, 0.025 } };
    double chessboard_corner_4_C[][4]{ {-0.1, 0.1, 0.025}, { -0.13, 0.1, 0.025 }, { -0.13, 0.13, 0.025 }, { -0.1, 0.13, 0.025 } };

    // Уголки с текстурой

    glEnable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    ruby_tex.Bind();

    glBegin(GL_QUADS);

    // Материал уголков
    float amb_ruby[] = { 0.1f,  0.02f, 0.02f,  1.0f };
    float dif_ruby[] = { 0.7f,  0.05f, 0.05f,  1.0f };
    float spec_ruby[] = { 1.0f, 0.3f,  0.3f,   1.0f };
    float sh_ruby = 0.9f * 128.0f;
    glMaterialfv(GL_FRONT, GL_AMBIENT, amb_ruby);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, dif_ruby);
    glMaterialfv(GL_FRONT, GL_SPECULAR, spec_ruby);
    glMaterialf(GL_FRONT, GL_SHININESS, sh_ruby);

    // Уголки
    // Верхняя часть
    glNormal3d(0, 0, 1);
    DrawTexturedQuad(chessboard_corner_1_C);
    DrawTexturedQuad(chessboard_corner_2_C);
    DrawTexturedQuad(chessboard_corner_3_C);
    DrawTexturedQuad(chessboard_corner_4_C);

    // Стенки
    glNormal3d(0, -1, 0);
    DrawTexturedQuad(chessboard_corner_1_W1);
    DrawTexturedQuad(chessboard_corner_2_W1);
    DrawTexturedQuad(chessboard_corner_3_W1);
    DrawTexturedQuad(chessboard_corner_4_W1);

    glNormal3d(0, 1, 0);
    DrawTexturedQuad(chessboard_corner_1_W3);
    DrawTexturedQuad(chessboard_corner_2_W3);
    DrawTexturedQuad(chessboard_corner_3_W3);
    DrawTexturedQuad(chessboard_corner_4_W3);

    glNormal3d(1, 0, 0);
    DrawTexturedQuad(chessboard_corner_1_W2);
    DrawTexturedQuad(chessboard_corner_2_W2);
    DrawTexturedQuad(chessboard_corner_3_W2);
    DrawTexturedQuad(chessboard_corner_4_W2);

    glNormal3d(-1, 0, 0);
    DrawTexturedQuad(chessboard_corner_1_W4);
    DrawTexturedQuad(chessboard_corner_2_W4);
    DrawTexturedQuad(chessboard_corner_3_W4);
    DrawTexturedQuad(chessboard_corner_4_W4);

    // Нижняя часть
    glNormal3d(0, 0, -1);
    DrawTexturedQuad(chessboard_corner_1_F);
    DrawTexturedQuad(chessboard_corner_2_F);
    DrawTexturedQuad(chessboard_corner_3_F);
    DrawTexturedQuad(chessboard_corner_4_F);

    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);

    glBegin(GL_QUADS);

    // Части доски без текстур
    // Материал доски
    float amb_board[] = { 0.1f, 0.05f, 0.02f, 1.0f };
    float dif_board[] = { 0.4f, 0.2f, 0.05f, 1.0f };
    float spec_board[] = { 0.2f, 0.15f, 0.1f, 1.0f };
    float sh_board = 0.1f * 128.0f;
    glMaterialfv(GL_FRONT, GL_AMBIENT, amb_board);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, dif_board);
    glMaterialfv(GL_FRONT, GL_SPECULAR, spec_board);
    glMaterialf(GL_FRONT, GL_SHININESS, sh_board);

    // Нижняя часть
    for (int i = 0; i < 4; i++)
        glVertex3dv(chessboard_F_Center[i]);
    for (int i = 0; i < 4; i++)
        glVertex3dv(chessboard_F_1[i]);
    for (int i = 0; i < 4; i++)
        glVertex3dv(chessboard_F_2[i]);
    for (int i = 0; i < 4; i++)
        glVertex3dv(chessboard_F_3[i]);
    for (int i = 0; i < 4; i++)
        glVertex3dv(chessboard_F_4[i]);

    // Гладкие боковые стенки
    Point N;
    const int segments = 40;
    double angle = (3.1415926535 / 2.0) / segments;
    double c = cos(angle);
    double s = sin(angle);

    // сторона 1
    double O_1[]{ 0.12, 0.0 };
    double A0_1[]{ 0.12, 0.01 };
    double y_front_1 = -0.1;
    double y_back_1 = 0.1;

    for (int i = 0; i < segments; i++) {
        double x = A0_1[0] - O_1[0];
        double z = A0_1[1] - O_1[1];

        double Ai[2] = { O_1[0] + x * c + z * s, O_1[1] - x * s + z * c };

        double pts[][4] = { {A0_1[0], y_front_1, A0_1[1], 0}, {A0_1[0], y_back_1,  A0_1[1], 0}, {Ai[0], y_back_1,  Ai[1], 0}, {0,0,0,0} };
        N = Normal(pts);
        glNormal3d(N.x, N.y, N.z);
        glVertex3d(A0_1[0], y_front_1, A0_1[1]);
        glVertex3d(A0_1[0], y_back_1, A0_1[1]);
        glVertex3d(Ai[0], y_back_1, Ai[1]);
        glVertex3d(Ai[0], y_front_1, Ai[1]);

        A0_1[0] = Ai[0];
        A0_1[1] = Ai[1];
    }

    // сторона 2
    double O_2[]{ -0.12, 0.0 };
    double A0_2[]{ -0.12, 0.01 };
    double y_front_2 = 0.1;
    double y_back_2 = -0.1;

    for (int i = 0; i < segments; i++) {
        double x = A0_2[0] - O_2[0];
        double z = A0_2[1] - O_2[1];

        double Ai[2] = { O_2[0] + x * c - z * s, O_2[1] + x * s + z * c };

        double pts[][4] = { {A0_2[0], y_front_2, A0_2[1], 0}, {A0_2[0], y_back_2,  A0_2[1], 0}, {Ai[0], y_back_2,  Ai[1], 0}, {0,0,0,0} };
        N = Normal(pts);
        glNormal3d(N.x, N.y, N.z);
        glVertex3d(A0_2[0], y_front_2, A0_2[1]);
        glVertex3d(A0_2[0], y_back_2, A0_2[1]);
        glVertex3d(Ai[0], y_back_2, Ai[1]);
        glVertex3d(Ai[0], y_front_2, Ai[1]);

        A0_2[0] = Ai[0];
        A0_2[1] = Ai[1];
    }

    // сторона 3
    double O_3[]{ -0.12, 0.0 };
    double A0_3[]{ -0.12, 0.01 };
    double x_left_1 = -0.1;
    double x_right_1 = 0.1;

    for (int i = 0; i < segments; i++) {
        double y = A0_3[0] - O_3[0];
        double z = A0_3[1] - O_3[1];

        double Ai[2] = { O_3[0] + y * c - z * s, O_3[1] + y * s + z * c };

        double pts[][4] = { {x_left_1,  A0_3[0], A0_3[1], 0}, {x_right_1, A0_3[0], A0_3[1], 0}, {x_right_1, Ai[0], Ai[1], 0}, {0,0,0,0} };
        N = Normal(pts);
        glNormal3d(N.x, N.y, N.z);
        glVertex3d(x_left_1, A0_3[0], A0_3[1]);
        glVertex3d(x_right_1, A0_3[0], A0_3[1]);
        glVertex3d(x_right_1, Ai[0], Ai[1]);
        glVertex3d(x_left_1, Ai[0], Ai[1]);

        A0_3[0] = Ai[0];
        A0_3[1] = Ai[1];
    }

    // сторона 4
    double O_4[]{ 0.12, 0.0 };
    double A0_4[]{ 0.12, 0.01 };
    double x_left_2 = 0.1;
    double x_right_2 = -0.1;

    for (int i = 0; i < segments; i++) {
        double y = A0_4[0] - O_4[0];
        double z = A0_4[1] - O_4[1];

        double Ai[2] = { O_4[0] + y * c + z * s, O_4[1] - y * s + z * c };

        double pts[][4] = { {x_left_2,  A0_4[0], A0_4[1], 0}, {x_right_2, A0_4[0], A0_4[1], 0}, {x_right_2, Ai[0], Ai[1], 0}, {0,0,0,0} };
        N = Normal(pts);
        glNormal3d(N.x, N.y, N.z);
        glVertex3d(x_left_2, A0_4[0], A0_4[1]);
        glVertex3d(x_right_2, A0_4[0], A0_4[1]);
        glVertex3d(x_right_2, Ai[0], Ai[1]);
        glVertex3d(x_left_2, Ai[0], Ai[1]);

        A0_4[0] = Ai[0];
        A0_4[1] = Ai[1];
    }

    // Верхняя часть
    glNormal3d(0, 0, 1);
    for (int i = 0; i < 4; i++)
        glVertex3dv(chessboard_C_1[i]);
    for (int i = 0; i < 4; i++)
        glVertex3dv(chessboard_C_2[i]);
    for (int i = 0; i < 4; i++)
        glVertex3dv(chessboard_C_3[i]);
    for (int i = 0; i < 4; i++)
        glVertex3dv(chessboard_C_4[i]);

    glEnd();


    // Часть с текстурой
    Chess_tex.Bind();

    glBegin(GL_QUADS);

    // Верхняя грань с текстурой
    glTexCoord2d(1, 1);
    glVertex3dv(chessboard_C_Center[0]);

    glTexCoord2d(1, 0);
    glVertex3dv(chessboard_C_Center[1]);

    glTexCoord2d(0, 0);
    glVertex3dv(chessboard_C_Center[2]);

    glTexCoord2d(0, 1);
    glVertex3dv(chessboard_C_Center[3]);

    glEnd();
    glPopMatrix();

    glBindTexture(GL_TEXTURE_2D, 0);

}

void DrawChessFigure(ObjModel& figure, double x, double y, double z = 0.569, bool flip = false, double rotationAngle = 0)
{
    glPushMatrix();
    glShadeModel(GL_SMOOTH);
    glTranslated(x, y, z);
    glScaled(0.015, 0.015, 0.015);
    glRotated(90, 1, 0, 0);
    if (flip)
        glRotated(180, 0, 1, 0);
    glRotated(rotationAngle, 1, 0, 0);  // Сальто
    figure.Draw();
    glPopMatrix();
}

void DrawPiece(ChessPiece& p)
{
    if (!p.alive)
        return;

    DrawChessFigure(*p.model, p.x, p.y, p.z, p.flip, p.rotationAngle);
}

void MovePiece(ChessPiece& p, double tx, double ty)
{
    p.targetX = tx;
    p.targetY = ty;
    p.moving = true;
}

double animationSpeed = 0.05;

void UpdatePiece(ChessPiece& p, double dt)
{
    if (!p.moving) return;

    double dx = p.targetX - p.x;
    double dy = p.targetY - p.y;
    double dist = sqrt(dx * dx + dy * dy);
    double step = animationSpeed * dt;

    if (step >= dist) {
        p.x = p.targetX;
        p.y = p.targetY;
        p.moving = false;
    }
    else {
        p.x += dx * step / dist;
        p.y += dy * step / dist;
    }
}

void ResetPieces()
{
    EmPawn5.x = 0.5125; EmPawn5.y = 0.4375;
    DiPawn5.x = 0.5125; DiPawn5.y = 0.5625;
    EmBishop2.x = 0.5375; EmBishop2.y = 0.4125;
    EmQueen.x = 0.4875; EmQueen.y = 0.4125;
    DiKnight1.x = 0.4375; DiKnight1.y = 0.5875;
    DiKnight2.x = 0.5625; DiKnight2.y = 0.5875;
    DiPawn6.alive = true;
}

void StartKingTrick()
{
    activeKing = (rand() % 2 == 0) ? &EmKing : &DiKing;

    activeKing->trickActive = true;
    activeKing->trickT = 0.0;
    activeKing->rotationAngle = 0.0;

    double x = activeKing->x;
    double y = activeKing->y;
    double z = activeKing->z;

    // Изогнутая кривая
    double height = 0.17;
    double offset = 0.2;

    kingPath[0] = { x, y, z };
    kingPath[1] = { x + offset, y, z + height };
    kingPath[2] = { x - offset, y, z + height };
    kingPath[3] = { x, y, z };
}

void UpdateKingStunt(double dt)
{
    if (!activeKing)
        return;

    activeKing->trickT += dt / activeKing->trickDuration;

    if (activeKing->trickT >= 1.0)
    {
        activeKing->trickT = 1.0;
        activeKing->trickActive = false;

        activeKing->x = kingPath[0].x;
        activeKing->y = kingPath[0].y;
        activeKing->z = kingPath[0].z;

        activeKing->rotationAngle = 0.0;

        return;
    }

    Point p = CubeBezier(kingPath, activeKing->trickT);

    activeKing->x = p.x;
    activeKing->y = p.y;
    activeKing->z = p.z;

    activeKing->rotationAngle = 360.0 * activeKing->trickT;
}

void Render(double delta_time)
{
    full_time += delta_time;

    // Настройка камеры и света
    if (gl.isKeyPressed('F')) // если нажата F - свет из камеры
    {
        light.SetPosition(camera.x(), camera.y(), camera.z());
    }
    camera.SetUpCamera();
    // Забираем матрицу MODELVIEW сразу после установки камеры,
    // так как в ней отсутствуют трансформации glRotate
    glGetFloatv(GL_MODELVIEW_MATRIX, view_matrix);

    light.SetUpLight();

    // Рисуем оси
    gl.DrawAxes();

    glBindTexture(GL_TEXTURE_2D, 0);

    // Включаем нормализацию нормалей
    // чтобы glScaled не влияли на них.

    glEnable(GL_NORMALIZE);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    // Переключаем режимы (см void switchModes(OpenGL *sender, KeyEventArg arg))
    if (lightning)
        glEnable(GL_LIGHTING);
    if (texturing)
    {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0); // Сбрасываем текущую текстуру
    }

    if (alpha)
    {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    //=============НАСТРОЙКА МАТЕРИАЛА==============

    // Настройка материала, все что рисуется ниже будет иметь этот материал.
    // Массивы с настройками материала

    // Материал стола
    float amb[] = { 0.1f, 0.1f, 0.1f, 1.0f };
    float dif[] = { 0.5f, 0.5f, 0.5f, 1.0f };
    float spec[] = { 0.8f, 0.8f, 0.8f, 1.0f }; 
    float sh = 0.6f * 128.0f;      

    // Фоновая
    glMaterialfv(GL_FRONT, GL_AMBIENT, amb);
    // Дифузная
    glMaterialfv(GL_FRONT, GL_DIFFUSE, dif);
    // Зеркальная
    glMaterialfv(GL_FRONT, GL_SPECULAR, spec);
    // Размер блика
    glMaterialf(GL_FRONT, GL_SHININESS, sh);

    // Сглаживание освещения
    glShadeModel(GL_SMOOTH); // закраска по Гуро
                             //(GL_SMOOTH - плоская закраска)

    //============ РИСОВАТЬ ТУТ ==============

    // Стол
    glPushMatrix();
    glShadeModel(GL_SMOOTH);
    glTranslated(0.5, 0.5, 0);
    glScaled(0.06, 0.06, 0.06);
    glRotated(90, 1, 0, 0);
    table.Draw();
    glPopMatrix();

    // Доска
	DrawChessBoard();

	// Шахматные фигуры
    
	// Изумрудные (белые)
 
    // Материал изумрудных шахматных фигур
    float amb_emerald[] = { 0.05f, 0.1f,  0.05f,  0.7f };
    float dif_emerald[] = { 0.2f,  0.6f,  0.2f,   0.7f };
    float spec_emerald[] = { 0.6f,  1.0f,  0.6f,   0.7f };
    float sh_emerald = 0.8f * 128.0f;
    glMaterialfv(GL_FRONT, GL_AMBIENT, amb_emerald);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, dif_emerald);
    glMaterialfv(GL_FRONT, GL_SPECULAR, spec_emerald);
    glMaterialf(GL_FRONT, GL_SHININESS, sh_emerald);

    emerald_tex.Bind();

	DrawPiece(EmRook1); DrawPiece(EmRook2);
    DrawPiece(EmKnight1); DrawPiece(EmKnight2);
    DrawPiece(EmBishop1); DrawPiece(EmBishop2);
	DrawPiece(EmQueen); DrawPiece(EmKing);
    DrawPiece(EmPawn1); DrawPiece(EmPawn2); DrawPiece(EmPawn3); DrawPiece(EmPawn4);
    DrawPiece(EmPawn5); DrawPiece(EmPawn6); DrawPiece(EmPawn7); DrawPiece(EmPawn8);

    glBindTexture(GL_TEXTURE_2D, 0);


	// Алмазные (черные)
    
    // Материал алмазных шахматных фигур
    float amb_diamond[] = { 0.02f, 0.06f, 0.15f, 0.7f };
    float dif_diamond[] = { 0.15f, 0.35f, 0.9f,  0.7f };
    float spec_diamond[] = { 1.0f,  1.0f,  1.0f,  0.7f };
    float sh_diamond = 0.95f * 128.0f;
    glMaterialfv(GL_FRONT, GL_AMBIENT, amb_diamond);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, dif_diamond);
    glMaterialfv(GL_FRONT, GL_SPECULAR, spec_diamond);
    glMaterialf(GL_FRONT, GL_SHININESS, sh_diamond);

    diamond_tex.Bind();

    DrawPiece(DiRook1); DrawPiece(DiRook2);
    DrawPiece(DiKnight1); DrawPiece(DiKnight2);
    DrawPiece(DiBishop1); DrawPiece(DiBishop2);
    DrawPiece(DiQueen); DrawPiece(DiKing);
    DrawPiece(DiPawn1); DrawPiece(DiPawn2); DrawPiece(DiPawn3); DrawPiece(DiPawn4);
    DrawPiece(DiPawn5); DrawPiece(DiPawn6); DrawPiece(DiPawn7); DrawPiece(DiPawn8);

    glBindTexture(GL_TEXTURE_2D, 0);


	// Анимация детского мата
    UpdatePiece(EmPawn5, delta_time);
    UpdatePiece(DiPawn5, delta_time);
    UpdatePiece(EmBishop2, delta_time);
    UpdatePiece(DiKnight1, delta_time);
    UpdatePiece(EmQueen, delta_time);
    UpdatePiece(DiKnight2, delta_time);

    if (mateAnimation)
    {
        stageTimer += delta_time;

        if (mateStage == 0)
        {
            MovePiece(EmPawn5, 0.5125, 0.4875);
            mateStage++;
        }
        else if (mateStage == 1 && !EmPawn5.moving)
        {
            MovePiece(DiPawn5, 0.5125, 0.5125);
            mateStage++;
        }
        else if (mateStage == 2 && !DiPawn5.moving)
        {
            MovePiece(EmBishop2, 0.4625, 0.4875);
            mateStage++;
        }
        else if (mateStage == 3 && !EmBishop2.moving)
        {
            MovePiece(DiKnight1, 0.4625, 0.5375);
            mateStage++;
        }
        else if (mateStage == 4 && !DiKnight1.moving)
        {
            MovePiece(EmQueen, 0.5875, 0.5125);
            mateStage++;
        }
        else if (mateStage == 5 && !EmQueen.moving)
        {
            MovePiece(DiKnight2, 0.5375, 0.5125);
            mateStage++;
        }
        else if (mateStage == 6 && !DiKnight2.moving)
        {
            MovePiece(EmQueen, 0.5375, 0.5625);
            mateStage++;
        }
        else if (mateStage == 7 && !EmQueen.moving)
        {
            DiPawn6.alive = false;
            mateAnimation = false;
        }
    }

	// Случайный король делает сальто
    UpdateKingStunt(delta_time);


    //===============================================

    // Сбрасываем все трансформации
    glLoadIdentity();
    camera.SetUpCamera();
    Shader::DontUseShaders();
    // Рисуем источник света
    light.DrawLightGizmo();

    //================Сообщение в верхнем левом углу=======================
    glActiveTexture(GL_TEXTURE0);
    // Переключаемся на матрицу проекции
    glMatrixMode(GL_PROJECTION);
    // Сохраняем текущую матрицу проекции с перспективным преобразованием
    glPushMatrix();
    // Загружаем единичную матрицу в матрицу проекции
    glLoadIdentity();

    // Устанавливаем матрицу параллельной проекции
    glOrtho(0, gl.getWidth() - 1, 0, gl.getHeight() - 1, 0, 1);

    // Переключаемся на матрицу MODELVIEW
    glMatrixMode(GL_MODELVIEW);
    // Сохраняем матрицу
    glPushMatrix();
    // Сбрасываем все трансформации и настройки камеры загрузкой единичной матрицы
    glLoadIdentity();

    // Нарисованное тут находится в 2D системе координат
    // Нижний левый угол окна - точка (0,0)
    // Верхний правый угол (ширина_окна - 1, высота_окна - 1)

    std::wstringstream ss;
    ss << std::fixed << std::setprecision(3) << "T - " << (texturing ? L"[вкл]выкл" : L"вкл[выкл]") << L" текстур\n"
       << "L - " << (lightning ? L"[вкл]выкл" : L"вкл[выкл]") << L" освещение\n"
       << "A - " << (alpha ? L"[вкл]выкл" : L"вкл[выкл]") << L" альфа-наложение\n"
	   << L"M - воспроизвести анимацию детского мата\n"
       << L"K - выполнить безумный королевский трюк\n"
       << L"F - переместить свет в позицию камеры\n"
       << L"G - двигать свет по горизонтали\n"
       << L"G+ЛКМ - двигать свет по вертикали\n"
       << L"Координаты света: (" << std::setw(7) << light.x() << "," << std::setw(7) << light.y() << "," << std::setw(7)
       << light.z() << ")\n"
       << L"Координаты камеры: (" << std::setw(7) << camera.x() << "," << std::setw(7) << camera.y() << ","
       << std::setw(7) << camera.z() << ")\n"
       << L"Параметры камеры: R=" << std::setw(7) << camera.distance() << ", fi1=" << std::setw(7) << camera.fi1()
       << ", fi2=" << std::setw(7) << camera.fi2() << '\n'
       << L"delta_time: " << std::setprecision(5) << delta_time << '\n'
       << L"full_time: " << std::setprecision(2) << full_time << std::endl;

    text.setPosition(10, gl.getHeight() - 10 - 260);
    text.setText(ss.str().c_str());
    text.Draw();

    // Восстанавливаем матрицу проекции на перспективу, которую сохраняли ранее.
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}
