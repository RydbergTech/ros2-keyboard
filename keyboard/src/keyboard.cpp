#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <format>

#include "rclcpp/rclcpp.hpp"

#include <keyboard_msgs/msg/key.hpp>

#include "SDL/SDL.h"
#include "SDL/SDL_ttf.h"

using namespace std::chrono_literals;

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

      window = SDL_SetVideoMode(600, 400, 0, 0);
    }

  private:
    void timer_callback() {

      keyboard_msgs::msg::Key k;
      bool pressed;
      bool new_event = false;
      SDL_Event event;

      SDL_Color textColor = { 255, 255, 255, 0 }; // White (R, G, B, A)
      SDL_Rect textLocation;

      SDL_Surface* textSurface;

      if (SDL_PollEvent(&event)) {

        switch(event.type) {
        
          case SDL_KEYUP: {

            pressed = false;
            k.code = event.key.keysym.sym;
            k.modifiers = event.key.keysym.mod;
            new_event = true;

            SDL_FillRect(window, NULL, SDL_MapRGB(window->format, k.code, 0, 0));

            formatted = std::format("Last KeyUp Code: {} {}", k.code, SDL_GetKeyName(event.key.keysym.sym));
            //const char* c_str_ptr = formatted.c_str();

            textSurface = TTF_RenderText_Blended(font, formatted.c_str(), textColor);

            textLocation = { 50, 50, 0, 0 }; // X and Y coordinates
            SDL_BlitSurface(textSurface, nullptr, window, &textLocation);

            SDL_Flip(window);
            
            break;

          } case SDL_KEYDOWN: {
            
            pressed = true;
            k.code = event.key.keysym.sym;
            k.modifiers = event.key.keysym.mod;
            new_event = true;

            SDL_FillRect(window, NULL, SDL_MapRGB(window->format, k.code, 0, 255)); 
            
            textSurface = TTF_RenderText_Blended(font, "Last KeyDown Code:", textColor);

            textLocation = { 50, 100, 0, 0 }; // X and Y coordinates
            SDL_BlitSurface(textSurface, nullptr, window, &textLocation);

            SDL_Flip(window);
            
            break;

          } case SDL_QUIT: {

            SDL_FreeSurface(window);
            TTF_CloseFont(font);
            TTF_Quit();
            SDL_Quit();
            timer->cancel();

            break;
          }
        }
      }

      if (new_event) {
        k.header.stamp = this->get_clock()->now();
        if (pressed) pub_down->publish(k);
        else pub_up->publish(k);
      }
    }

    // Class member declarations
    rclcpp::TimerBase::SharedPtr timer;

    rclcpp::Publisher<keyboard_msgs::msg::Key>::SharedPtr pub_down;
    rclcpp::Publisher<keyboard_msgs::msg::Key>::SharedPtr pub_up;

    SDL_Surface* window;
    TTF_Font* font;
    std::string formatted;
  };

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<KeyboardNode>());
  rclcpp::shutdown();
  return 0;
}
