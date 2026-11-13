#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <format>
#include <filesystem>
#include <iostream>

#include "rclcpp/rclcpp.hpp"
#include "ament_index_cpp/get_package_share_directory.hpp"

#include <keyboard_msgs/msg/key.hpp>

#include "SDL/SDL.h"
#include "SDL/SDL_ttf.h"
#include "SDL/SDL_image.h"
#include "SDL/SDL_rotozoom.h"

using namespace std::chrono_literals;

SDL_Surface* load_scale_png(const std::string& filename, int targetWidth, int targetHeight) {
  SDL_Surface* loadedImage = nullptr;
  SDL_Surface* scaledSurface = nullptr;
  SDL_Surface* optimizedImage = nullptr;

  // 1. Load the PNG using SDL_image
  loadedImage = IMG_Load(filename.c_str());
  if (loadedImage == nullptr) {
      std::cerr << "Unable to load image " << filename << "! SDL_image Error: " << IMG_GetError() << std::endl;
      return nullptr;
  }

  // Calculate floating-point zoom factors from source and target dimensions
  double scaleX = static_cast<double>(targetWidth) / static_cast<double>(loadedImage->w);
  double scaleY = static_cast<double>(targetHeight) / static_cast<double>(loadedImage->h);

  // 2. Downscale the surface using zoomSurface (SMOOTHING_ON enables anti-aliasing)
  scaledSurface = zoomSurface(loadedImage, scaleX, scaleY, SMOOTHING_ON);
  
  // Free the original full-size surface to prevent memory leaks
  SDL_FreeSurface(loadedImage);

  if (!scaledSurface) {
      std::cerr << "zoomSurface failed." << std::endl;
      return nullptr;
  }

  // 3. Convert surface to screen format with alpha channel support
  // Consensus from SDL developers: use SDL_DisplayFormatAlpha for transparent PNGs in SDL 1.2
  optimizedImage = SDL_DisplayFormatAlpha(scaledSurface);

  // 3. Free the old unoptimized surface
  SDL_FreeSurface(scaledSurface);

  return optimizedImage;
}

class KeyboardNode : public rclcpp::Node {
  public:
    KeyboardNode() : Node("keyboard_node") {
      pub_down = this->create_publisher<keyboard_msgs::msg::Key>("keydown", 10);
      pub_up = this->create_publisher<keyboard_msgs::msg::Key>("keyup", 10);
      timer = this->create_wall_timer(20ms, std::bind(&KeyboardNode::timer_callback, this));

      this->declare_parameter("allow_repeat", false);
      bool allow_repeat = this->get_parameter("allow_repeat").as_bool();

      this->declare_parameter("repeat_delay", SDL_DEFAULT_REPEAT_DELAY);
      int repeat_delay = this->get_parameter("repeat_delay").as_int();

      this->declare_parameter("repeat_interval", SDL_DEFAULT_REPEAT_INTERVAL);
      int repeat_interval = this->get_parameter("repeat_interval").as_int();
    
      if ( !allow_repeat ) repeat_delay=0; // disable

      if (SDL_Init(SDL_INIT_VIDEO) < 0) throw std::runtime_error("Could not init SDL");

      if (TTF_Init() < 0) {
        SDL_Quit();
        throw std::runtime_error("Could not init TTF_Init");
      }

      //TTF_Font* font = TTF_OpenFont("arial.ttf", 28);
      font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 32);

      if (!font) {
          std::cerr << "TTF_OpenFont failed: " << TTF_GetError() << "\n";
          TTF_Quit();
          SDL_Quit();
          throw std::runtime_error("Could not init TTF_Init");
      }

      SDL_EnableKeyRepeat( repeat_delay, repeat_interval );

      SDL_WM_SetCaption("RydbergTech ROS Keyboard Input", NULL);

      window = SDL_SetVideoMode(600, 800, 0, 0);

      std::filesystem::path share_dir = 
        ament_index_cpp::get_package_share_directory("keyboard");
      
      // 2. Append the relative path to your target file
      std::filesystem::path file_path = share_dir / "config" / "rt_logo.png";

      // Convert to std::string if required by downstream APIs
      std::string file_path_str = file_path.string();

      logo = load_scale_png(file_path_str.c_str(), 500, 88);

      help_strs = {
        "> space : pause plan stepping",
        "> home  : reset plan index",
        "> arrow up  : run forward",
        "> arrow down : run backward",
        "> arrow right : step next",
        "> arrow left : step back",
      };
      
      keyup_txt = "---";
      keydown_txt = "---";
      mapped_color = 0;
      //keyColorBoxLocation = {0, 0, 0, 0};
    }

  private:
    void timer_callback() {

      keyboard_msgs::msg::Key k;
      bool pressed = false;
      bool new_event = false;
      bool shutdown = false;

      SDL_Event event;
      SDL_Color textColor = { 255, 255, 255, 0 }; // White (R, G, B, unused)
      SDL_Rect textLocation;
      SDL_Surface* textSurface;

      if (SDL_PollEvent(&event)) {

        switch(event.type) {
          
          case SDL_KEYDOWN:

            pressed = true;
            [[fallthrough]]; // Suppresses the warning for falling into SDL_KEYUP case

          case SDL_KEYUP: {
            
            new_event = true;

            k.code = event.key.keysym.sym;
            k.modifiers = event.key.keysym.mod;
            
            break;

          } case SDL_QUIT: {

            shutdown = true;

            SDL_FreeSurface(logo);
            SDL_FreeSurface(window);
            TTF_CloseFont(font);
            TTF_Quit();
            SDL_Quit();
            timer->cancel();

            break;
          }
        }
      }
      
      if (!shutdown) {
        
        // Blank window first
        SDL_FillRect(window, NULL, SDL_MapRGB(window->format, 0, 0, 0));
        
        // Logo lighter background
        SDL_Rect logoBgDestRect = { 20, 20, 560, 148 };
        SDL_FillRect(window, &logoBgDestRect, SDL_MapRGB(window->format, 128, 128, 128));
        
        // Blit the resized logo onto the screen
        SDL_Rect destRect = { 50, 50, 0, 0 };
        SDL_BlitSurface(logo, nullptr, window, &destRect);
        
        Uint8 key_r=0, key_g=0, key_b=0;

        if(new_event) {

          // Symbols, numbers, return, space
          if (k.code < SDLK_a) {
            key_r = 128;
            key_g = (255 * k.code) / SDLK_a;

          // Letter keys
          } else if (k.code <= SDLK_z) {
            key_g = 128;
            key_b = (255 * (k.code - SDLK_a)) / (SDLK_z - SDLK_a);

          // Function keys (arrows, shift, crtlr, locks)
          } else {
            key_b = 164;
            key_r = (255 * (k.code - SDLK_z)) / (SDLK_LAST - SDLK_z);
          }

          mapped_color = SDL_MapRGB(window->format, key_r, key_g, key_b);

          char* key_str = SDL_GetKeyName(event.key.keysym.sym);

          if (pressed) {
            keyColorBoxLocation = {20, 330, 60, 60};
            keyup_txt = "---";
            keydown_txt = std::format(" {} | {}", k.code, key_str);

          } else {
            keyColorBoxLocation = {20, 250, 60, 60};
            keyup_txt = std::format(" {} | {}", k.code, key_str);
            keydown_txt = "---";
          }
          
          // Stamp and send ROS message to up or down topic
          k.header.stamp = this->get_clock()->now();
          k.header.frame_id = key_str;

          if (pressed)
            pub_down->publish(k);

          else 
            pub_up->publish(k);
        }

        textLocation = { 30, 200, 0, 0 };
        textSurface = TTF_RenderText_Blended(font, "===  Last Key Event ===", textColor);
        SDL_BlitSurface(textSurface, nullptr, window, &textLocation);
        SDL_FreeSurface(textSurface);

        // Color box representing key code class
        SDL_FillRect(window, &keyColorBoxLocation, mapped_color);

        textLocation = { 100, 260, 0, 0 };
        formatted = std::format("KeyUp : {}", keyup_txt);
        textSurface = TTF_RenderText_Blended(font, formatted.c_str(), textColor);
        SDL_BlitSurface(textSurface, nullptr, window, &textLocation);
        SDL_FreeSurface(textSurface);

        textLocation = { 100, 340, 0, 0 };
        formatted = std::format("KeyDown : {}", keydown_txt);
        textSurface = TTF_RenderText_Blended(font, formatted.c_str(), textColor);
        SDL_BlitSurface(textSurface, nullptr, window, &textLocation);
        SDL_FreeSurface(textSurface);
        
        Sint16 help_txt_row_start = 420; // nice

        textLocation = { 30, help_txt_row_start, 0, 0 };
        textSurface = TTF_RenderText_Blended(font, "===  Command Interface ===", textColor);
        SDL_BlitSurface(textSurface, nullptr, window, &textLocation);
        SDL_FreeSurface(textSurface);

        help_txt_row_start += 60;
        
        for (const auto& help_txt : help_strs) {
          
          textSurface = TTF_RenderText_Blended(font, help_txt.c_str(), textColor);
          textLocation = { 60, help_txt_row_start, 0, 0 };

          SDL_BlitSurface(textSurface, nullptr, window, &textLocation);
          SDL_FreeSurface(textSurface);

          help_txt_row_start += 50;
        }

        SDL_Flip(window);
      }
    }
    
    // Class member declarations
    rclcpp::TimerBase::SharedPtr timer;

    rclcpp::Publisher<keyboard_msgs::msg::Key>::SharedPtr pub_down;
    rclcpp::Publisher<keyboard_msgs::msg::Key>::SharedPtr pub_up;

    SDL_Surface* window, *logo;
    TTF_Font* font;
    std::string formatted, keyup_txt, keydown_txt;
    std::vector<std::string> help_strs;
    SDL_Rect keyColorBoxLocation;
    Uint32 mapped_color;
  };

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<KeyboardNode>());
  rclcpp::shutdown();
  return 0;
}
