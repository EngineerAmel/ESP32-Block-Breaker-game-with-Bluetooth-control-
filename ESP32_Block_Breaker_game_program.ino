#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DabbleESP32.h>

// Screen and Display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, 16, 17, 5); // Adjust pins for your setup

// Game Constants
#define SOUND_PIN 25
#define PADDLE_HEIGHT 4
#define BALL_SIZE 3
#define BRICK_ROWS 4
#define BRICK_COLS 6
#define BRICK_WIDTH (SCREEN_WIDTH / BRICK_COLS)
#define BRICK_HEIGHT 8
#define MAX_LIVES 5
#define MAX_PARTICLES 30
#define MAX_POWERUPS 3
#define MAX_BALLS 2

// Data Structures for Game Objects
struct Particle { int x, y, t; };
struct PowerUp { int x, y, type; bool active; };
struct Ball { float x, y, dx, dy; bool active; };

// Game State Variables
int level = 1;
const char* levels[] = {"Easy","Normal","Hard"};
bool selecting = true;
bool gameRunning = false;
int paddleW, paddleX, score, lives;
bool bricks[BRICK_ROWS][BRICK_COLS];

// Object Pools
Particle parts[MAX_PARTICLES];
PowerUp ups[MAX_POWERUPS];
Ball balls[MAX_BALLS];

// --- Sound Functions ---
void toneT(int f, int d) {
  ledcSetup(0, f, 8);
  ledcAttachPin(SOUND_PIN, 0);
  ledcWriteTone(0, f);
  delay(d);
  ledcWriteTone(0, 0);
}

void soundWall() { toneT(600, 30); }
void soundPaddle() { toneT(1200, 30); }
void soundBrick() { toneT(1400, 40); }
void soundLose() { toneT(400, 100); }
void soundWin() { toneT(1500, 500); }

// Non-blocking background sound
unsigned long lastBgSoundTime = 0;
int bgNote = 0;
void bgLoop() {
  if (millis() - lastBgSoundTime > 100) {
    if (bgNote == 0) toneT(300, 40);
    else toneT(400, 40);
    bgNote = (bgNote + 1) % 2;
    lastBgSoundTime = millis();
  }
}

// --- Game Object Management ---
void spawnParticles(int x, int y) {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    parts[i] = {x + random(-5, 6), y + random(-5, 6), random(10, 20)};
  }
}

void spawnPowerUp(int x, int y) {
  for (auto &u : ups) {
    if (!u.active) {
      u = {x, y, random(0, 3), true};
      break;
    }
  }
}

// --- Game State Management ---
void resetGame() {
  paddleW = 40 - level * 7;
  paddleX = (SCREEN_WIDTH - paddleW) / 2;
  score = 0;
  lives = MAX_LIVES;

  // Initialize all bricks as visible
  for (int r = 0; r < BRICK_ROWS; r++) {
    for (int c = 0; c < BRICK_COLS; c++) {
      bricks[r][c] = true;
    }
  }

  // Reset ball(s)
  for(int i = 0; i < MAX_BALLS; i++){
    balls[i] = {
      (float)SCREEN_WIDTH / 2, (float)SCREEN_HEIGHT - 20,
      cos(random(45, 135) * PI / 180.0f) * (2.0f + level * 0.5f),
      -abs(sin(random(45, 135) * PI / 180.0f)) * (2.0f + level * 0.5f),
      i == 0 // Only the first ball is active initially
    };
  }
  
  // Deactivate all power-ups
  for (auto &u : ups) u.active = false;
  display.clearDisplay();
}

void drawMenu() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 0);
  display.println("BBreakerX");
  display.setTextSize(1);
  display.setCursor(20, 25);
  display.println("Select Level:");
  for (int i = 0; i < 3; i++) {
    display.setCursor(30, 40 + i * 10);
    if (i == level) display.print("> ");
    else display.print("  ");
    display.println(levels[i]);
  }
  display.display();
}

void showEndGameMessage(const char* message) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor((SCREEN_WIDTH - strlen(message)*12)/2, SCREEN_HEIGHT/2 - 8);
    display.println(message);
    display.display();
    delay(2000); // Show message for 2 seconds
}

// --- Main Setup and Loop ---
void setup() {
  Serial.begin(115200);
  Dabble.begin("BBreakerX"); // Name your ESP32 for the Dabble App
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  display.clearDisplay();
  drawMenu();
}

void updateGame() {
  // --- Paddle Movement ---
  if (GamePad.isLeftPressed()) {
    paddleX -= 5;
    if (paddleX < 0) paddleX = 0;
  }
  if (GamePad.isRightPressed()) {
    paddleX += 5;
    if (paddleX + paddleW > SCREEN_WIDTH) paddleX = SCREEN_WIDTH - paddleW;
  }

  // --- Ball Logic ---
  bool allBallsDead = true;
  for (auto &b : balls) {
    if (!b.active) continue;
    allBallsDead = false;

    b.x += b.dx;
    b.y += b.dy;

    // Wall collision
    if (b.x - BALL_SIZE <= 0 || b.x + BALL_SIZE >= SCREEN_WIDTH) { b.dx = -b.dx; soundWall(); }
    if (b.y - BALL_SIZE <= 0) { b.dy = -b.dy; soundWall(); }

    // Paddle collision
    if (b.dy > 0 && b.y + BALL_SIZE >= SCREEN_HEIGHT - PADDLE_HEIGHT && b.x >= paddleX && b.x <= paddleX + paddleW) {
      b.dy = -b.dy;
      // Change angle based on where it hits the paddle
      float hitPos = (b.x - (paddleX + paddleW / 2)) / (paddleW / 2); // -1 to 1
      b.dx += hitPos * 0.5;
      soundPaddle();
    }

    // Brick collision
    for (int r = 0; r < BRICK_ROWS; r++) {
      for (int c = 0; c < BRICK_COLS; c++) {
        if (!bricks[r][c]) continue;
        int bx = c * BRICK_WIDTH;
        int by = r * BRICK_HEIGHT;
        if (b.x > bx && b.x < bx + BRICK_WIDTH && b.y > by && b.y < by + BRICK_HEIGHT) {
          bricks[r][c] = false; // Destroy brick
          score += 10;
          soundBrick();
          spawnParticles(bx + BRICK_WIDTH / 2, by + BRICK_HEIGHT / 2);
          if (random(0, 5) == 0) spawnPowerUp(bx + BRICK_WIDTH / 2, by + BRICK_HEIGHT / 2);
          
          // Improved bounce logic
          float overlapX = (BRICK_WIDTH / 2 + BALL_SIZE) - abs(b.x - (bx + BRICK_WIDTH / 2));
          float overlapY = (BRICK_HEIGHT / 2 + BALL_SIZE) - abs(b.y - (by + BRICK_HEIGHT / 2));
          if(overlapX < overlapY) b.dx = -b.dx;
          else b.dy = -b.dy;
        }
      }
    }
    
    // Ball lost
    if (b.y > SCREEN_HEIGHT) b.active = false;
  }

  if (allBallsDead) {
    lives--;
    soundLose();
    if (lives > 0) {
        // Relaunch a single ball
        balls[0].active = true;
        balls[0].x = paddleX + paddleW / 2;
        balls[0].y = SCREEN_HEIGHT - 20;
        balls[0].dy = -abs(balls[0].dy);
    } else {
        showEndGameMessage("GAME OVER");
        gameRunning = false;
        selecting = true;
    }
  }

  // --- Check for Win Condition ---
  bool allBricksGone = true;
  for (int r = 0; r < BRICK_ROWS; r++) {
    for (int c = 0; c < BRICK_COLS; c++) {
      if (bricks[r][c]) allBricksGone = false;
    }
  }
  if (allBricksGone) {
    soundWin();
    showEndGameMessage("YOU WIN!");
    gameRunning = false;
    selecting = true;
  }

  // --- Power-Up Logic ---
  for (auto &u : ups) {
    if (u.active) {
      u.y++;
      if (u.y > SCREEN_HEIGHT) u.active = false;
      // Check for paddle collision
      if (u.y > SCREEN_HEIGHT - PADDLE_HEIGHT && u.x > paddleX && u.x < paddleX + paddleW) {
        u.active = false;
        if (u.type == 0) paddleW = min(paddleW + 15, SCREEN_WIDTH - 10); // Bigger paddle
        else if (u.type == 1 && lives < MAX_LIVES) lives++; // Extra life
        else if (u.type == 2) { // Multiball
          for (auto &ball : balls) if(!ball.active) { ball.active = true; break;}
        }
      }
    }
  }
}

void drawGame() {
  display.clearDisplay();
  
  // Draw particles
  for (auto &p : parts) {
    if (p.t-- > 0) {
      display.drawPixel(p.x, p.y, SSD1306_WHITE);
      p.y += 1;
    }
  }

  // Draw bricks
  for (int r = 0; r < BRICK_ROWS; r++) {
    for (int c = 0; c < BRICK_COLS; c++) {
      if (bricks[r][c]) {
        display.drawRect(c * BRICK_WIDTH, r * BRICK_HEIGHT, BRICK_WIDTH, BRICK_HEIGHT, SSD1306_WHITE);
      }
    }
  }
  
  // Draw paddle
  display.fillRoundRect(paddleX, SCREEN_HEIGHT - PADDLE_HEIGHT, paddleW, PADDLE_HEIGHT, 2, SSD1306_WHITE);

  // Draw balls
  for (auto &b : balls) if (b.active) display.fillCircle(b.x, b.y, BALL_SIZE, SSD1306_WHITE);

  // Draw powerups
  for (auto &u : ups) if (u.active) display.fillRect(u.x, u.y, 6, 6, SSD1306_WHITE);

  // Draw score and lives
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("L:%d S:%d", lives, score);
  
  display.display();
}

void loop() {
  Dabble.processInput();

  if (selecting) {
    if (GamePad.isUpPressed()) { level = (level + 2) % 3; delay(150); }
    if (GamePad.isDownPressed()) { level = (level + 1) % 3; delay(150); }
    drawMenu();
    if (GamePad.isSelectPressed()) {
      selecting = false;
      gameRunning = true;
      resetGame();
      delay(200);
    }
    return;
  }
  
  if (gameRunning) {
    bgLoop();
    updateGame();
    drawGame();
    delay(20); // Frame delay
  } else {
    // If game has ended, go back to selection mode
    selecting = true;
    drawMenu();
    delay(200);
  }
}