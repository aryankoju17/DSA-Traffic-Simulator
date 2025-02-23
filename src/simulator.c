#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h> 
#include <stdio.h> 
#include <string.h>
#include <time.h>

#define MAX_LINE_LENGTH 20
#define MAIN_FONT "/usr/share/fonts/TTF/DejaVuSans.ttf"
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 800
#define SCALE 1
#define ROAD_WIDTH 150
#define LANE_WIDTH 50
#define MAX_VEHICLES 20
#define PRIORITY_THRESHOLD 10

const char* VEHICLE_FILE = "vehicles.data";

typedef struct {
    char vehicleNumber[10];
    time_t timestamp;
} Vehicle;

typedef struct {
    Vehicle vehicles[MAX_VEHICLES];
    int front;
    int rear;
    int count;
    SDL_mutex* mutex;
} VehicleQueue;

typedef struct {
    VehicleQueue lanes[3]; // Assuming 3 lanes per road
} Road;

typedef struct {
    int currentLight[4]; // Current state of the lights for roads A, B, C, D
    int priorityLane;     // Index for the priority lane
    Road roads[4];        // Four roads (A, B, C, D)
    SDL_mutex* stateMutex;
} SharedData;

// Function declarations
bool initializeSDL(SDL_Window **window, SDL_Renderer **renderer);
void drawRoadsAndLane(SDL_Renderer *renderer);
void displayText(SDL_Renderer *renderer, TTF_Font *font, char *text, int x, int y);
void drawTrafficLights(SDL_Renderer* renderer, int x, int y, int state);
void refreshLight(SDL_Renderer *renderer, SharedData* sharedData);
void* chequeQueue(void* arg);
void* readAndParseFile(void* arg);
void enqueue(VehicleQueue* queue, Vehicle vehicle);
Vehicle dequeue(VehicleQueue* queue);
bool isQueueFull(VehicleQueue* queue);
bool isQueueEmpty(VehicleQueue* queue);

int main(int argc, char* argv[]) {
    pthread_t tQueue, tReadFile;
    SDL_Window* window = NULL;
    SDL_Renderer* renderer = NULL;    
    SDL_Event event;    

    if (!initializeSDL(&window, &renderer)) {
        return -1;
    }

    SharedData sharedData; 
    sharedData.priorityLane = 0; // Start with lane A as priority
    for (int i = 0; i < 4; i++) {
        sharedData.currentLight[i] = 1; // Start with all lights red
        sharedData.roads[i].lanes[0].front = 0;
        sharedData.roads[i].lanes[0].rear = 0;
        sharedData.roads[i].lanes[0].count = 0;
        sharedData.roads[i].lanes[0].mutex = SDL_CreateMutex();
        sharedData.roads[i].lanes[1].front = 0;
        sharedData.roads[i].lanes[1].rear = 0;
        sharedData.roads[i].lanes[1].count = 0;
        sharedData.roads[i].lanes[1].mutex = SDL_CreateMutex();
        sharedData.roads[i].lanes[2].front = 0;
        sharedData.roads[i].lanes[2].rear = 0;
        sharedData.roads[i].lanes[2].count = 0;
        sharedData.roads[i].lanes[2].mutex = SDL_CreateMutex();
    }
    
    TTF_Font* font = TTF_OpenFont(MAIN_FONT, 24);
    if (!font) SDL_Log("Failed to load font: %s", TTF_GetError());

    // Initial drawing
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderClear(renderer);
    drawRoadsAndLane(renderer);
    SDL_RenderPresent(renderer);

    pthread_create(&tQueue, NULL, chequeQueue, &sharedData);
    pthread_create(&tReadFile, NULL, readAndParseFile, NULL);

    // Continue the UI thread
    bool running = true;
    while (running) {
        refreshLight(renderer, &sharedData);
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
        }
    }

    // Clean up
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) {
            SDL_DestroyMutex(sharedData.roads[i].lanes[j].mutex);
        }
    }
    
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

bool initializeSDL(SDL_Window **window, SDL_Renderer **renderer) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
        return false;
    }
    if (TTF_Init() < 0) {
        SDL_Log("SDL_ttf could not initialize! TTF_Error: %s\n", TTF_GetError());
        return false;
    }

    *window = SDL_CreateWindow("Traffic Light Management",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               WINDOW_WIDTH*SCALE, WINDOW_HEIGHT*SCALE,
                               SDL_WINDOW_SHOWN);
    if (!*window) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        SDL_Quit();
        return false;
    }

    *renderer = SDL_CreateRenderer(*window, -1, SDL_RENDERER_ACCELERATED);
    SDL_RenderSetScale(*renderer, SCALE, SCALE);

    if (!*renderer) {
        SDL_Log("Failed to create renderer: %s", SDL_GetError());
        SDL_DestroyWindow(*window);
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    return true;
}

void drawTrafficLights(SDL_Renderer* renderer, int x, int y, int state) {
    // Draw light box
    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255); // Gray light box
    SDL_Rect lightBox = {x, y, 40, 80}; // Larger box for better visibility
    SDL_RenderFillRect(renderer, &lightBox);
    
    // Draw Red Light
    SDL_SetRenderDrawColor(renderer, state == 1 ? 255 : 0, 0, 0, 255); // Red if state is 1
    SDL_Rect redLight = {x + 10, y + 10, 40, 40}; // Red light
    SDL_RenderFillRect(renderer, &redLight);

    // Draw Green Light
    SDL_SetRenderDrawColor(renderer, state == 2 ? 0 : 0, 255, 0, 255); // Green if state is 2
    SDL_Rect greenLight = {x + 10 , y + 55 , 40, 40}; // Green light
    SDL_RenderFillRect(renderer, &greenLight);
}

void refreshLight(SDL_Renderer *renderer, SharedData* sharedData) {
    // Clear the previous frame
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // White background
    SDL_RenderClear(renderer);

    // Draw roads and lanes
    drawRoadsAndLane(renderer);

    // Draw traffic lights based on their states
    drawTrafficLights(renderer, 350, 250, sharedData->currentLight[0]); // Road A
    drawTrafficLights(renderer, 350, 650, sharedData->currentLight[1]); // Road B
    drawTrafficLights(renderer, 250, 350, sharedData->currentLight[2]); // Road C
    drawTrafficLights(renderer, 650, 350, sharedData->currentLight[3]); // Road D

    SDL_RenderPresent(renderer);
}

void drawRoadsAndLane(SDL_Renderer *renderer) {
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255); // Dark gray color for roads
    
    // Vertical road
    SDL_Rect verticalRoad = {WINDOW_WIDTH / 2 - ROAD_WIDTH / 2, 0, ROAD_WIDTH, WINDOW_HEIGHT};
    SDL_RenderFillRect(renderer, &verticalRoad);

    // Horizontal road
    SDL_Rect horizontalRoad = {0, WINDOW_HEIGHT / 2 - ROAD_WIDTH / 2, WINDOW_WIDTH, ROAD_WIDTH};
    SDL_RenderFillRect(renderer, &horizontalRoad);

    // Draw horizontal lanes
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // White for lane markings
    for (int i = 0; i <= 3; i++) {
        // Horizontal lanes
        SDL_RenderDrawLine(renderer, 
            0, WINDOW_HEIGHT / 2 - ROAD_WIDTH / 2 + LANE_WIDTH * i,  // x1,y1
            WINDOW_WIDTH / 2 - ROAD_WIDTH / 2, WINDOW_HEIGHT / 2 - ROAD_WIDTH / 2 + LANE_WIDTH * i // x2, y2
        );
        SDL_RenderDrawLine(renderer, 
            WINDOW_WIDTH, WINDOW_HEIGHT / 2 - ROAD_WIDTH / 2 + LANE_WIDTH * i,
            WINDOW_WIDTH / 2 + ROAD_WIDTH / 2, WINDOW_HEIGHT / 2 - ROAD_WIDTH / 2 + LANE_WIDTH * i
        );

        // Vertical lanes
        SDL_RenderDrawLine(renderer,
            WINDOW_WIDTH / 2 - ROAD_WIDTH / 2 + LANE_WIDTH * i, 0,
            WINDOW_WIDTH / 2 - ROAD_WIDTH / 2 + LANE_WIDTH * i, WINDOW_HEIGHT / 2 - ROAD_WIDTH / 2
        );
        SDL_RenderDrawLine(renderer,
            WINDOW_WIDTH / 2 - ROAD_WIDTH / 2 + LANE_WIDTH * i, WINDOW_HEIGHT,
            WINDOW_WIDTH / 2 - ROAD_WIDTH / 2 + LANE_WIDTH * i, WINDOW_HEIGHT / 2 + ROAD_WIDTH / 2
        );
    }
    displayText(renderer, NULL, "A", 400, 10);
    displayText(renderer, NULL, "B", 400, 770);
    displayText(renderer, NULL, "D", 10, 400);
    displayText(renderer, NULL, "C", 770, 400);
}

void displayText(SDL_Renderer *renderer, TTF_Font *font, char *text, int x, int y) {
    SDL_Color textColor = {255, 255, 255, 255}; // White color for text
    SDL_Surface *textSurface = TTF_RenderText_Solid(font, text, textColor);
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, textSurface);
    SDL_FreeSurface(textSurface);
    SDL_Rect textRect = {x, y, 0, 0};
    SDL_QueryTexture(texture, NULL, NULL, &textRect.w, &textRect.h);
    SDL_RenderCopy(renderer, texture, NULL, &textRect);
}

void* chequeQueue(void* arg) {
    SharedData* sharedData = (SharedData*)arg;
    while (1) {
        // Normal handling logic
        for (int i = 0; i < 4; i++) {
            SDL_LockMutex(sharedData->stateMutex);
            sharedData->currentLight[i] = (i == sharedData->priorityLane) ? 2 : 1; // Green for priority lane
            SDL_UnlockMutex(sharedData->stateMutex);
            sleep(5); // Duration for green light
            SDL_LockMutex(sharedData->stateMutex);
            sharedData->currentLight[i] = 1; // Switch back to red
            SDL_UnlockMutex(sharedData->stateMutex);
        }
    }
    return NULL;
}

void* readAndParseFile(void* arg) {
    while (1) { 
        FILE* file = fopen(VEHICLE_FILE, "r");
        if (!file) {
            perror("Error opening file");
            continue;
        }

        char line[MAX_LINE_LENGTH];
        while (fgets(line, sizeof(line), file)) {
            line[strcspn(line, "\n")] = 0; // Remove newline
            char* vehicleNumber = strtok(line, ":");
            char* road = strtok(NULL, ":");
            if (vehicleNumber && road) {
                printf("Vehicle: %s, Road: %s\n", vehicleNumber, road);
            } else {
                printf("Invalid format: %s\n", line);
            }
        }
        fclose(file);
        SDL_Delay(2000); // Sleep for 2 seconds
    }
    return NULL;
}

void enqueue(VehicleQueue* queue, Vehicle vehicle) {
    SDL_LockMutex(queue->mutex);
    if (queue->count < MAX_VEHICLES) {
        queue->vehicles[queue->rear] = vehicle;
        queue->rear = (queue->rear + 1) % MAX_VEHICLES;
        queue->count++;
    }
    SDL_UnlockMutex(queue->mutex);
}

Vehicle dequeue(VehicleQueue* queue) {
    SDL_LockMutex(queue->mutex);
    Vehicle vehicle = { "", 0 };
    if (queue->count > 0) {
        vehicle = queue->vehicles[queue->front];
        queue->front = (queue->front + 1) % MAX_VEHICLES;
        queue->count--;
    }
    SDL_UnlockMutex(queue->mutex);
    return vehicle;
}

bool isQueueFull(VehicleQueue* queue) {
    return queue->count == MAX_VEHICLES;
}

bool isQueueEmpty(VehicleQueue* queue) {
    return queue->count == 0;
}