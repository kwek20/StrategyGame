#ifndef __FPS_MANAGER_H_INCLUDED__
#define __FPS_MANAGER_H_INCLUDED__

#include <string>
#include <iostream>
#include <allegro5\allegro.h>

#define MIN_TARGET_FPS 20.0
#define MAX_TARGET_FPS 60.0

/** The FpsManager class is designed to work with GLFW and enforces a specified framerate on an application.
  * It can also display the current framerate at user-specified intervals, and in addition returns the time
  * duration since the last frame, which can be used to implement framerate independent movement.
  *
  * Author: r3dux
  * Revision: 0.3
  * Date: 1st September 2013
  *
  * ---- Creation examples (it's most useful to create your fpsManager object globally in your Main.cpp file): ----
  *
  *     FpsManager fpsManager(60.0);                    // Lock to 60fps, no reporting of framerate
  *
  *     FpsManager fpsManager(85.0, 3.0);               // Lock to 85fps, output FPS to console once every three seconds
  *
  *     FpsManager fpsManager(30.0, 0.5, "My App");     // Lock to 30fps, output FPS to console & window title every half second
  *
  *
  * ---- Using the fpsManager in your main loop: ----
  *
  * bool running     = true;
  * double deltaTime = 0.0;
  *
  * while (running)
  * {
  *     // Calculate our camera movement
  *     cam->move(deltaTime);
  *
  *     // Draw our scene
  *     drawScene();
  *
  *     // Exit if ESC was pressed or window was closed
  *     running = !glfwGetKey(GLFW_KEY_ESC) && glfwGetWindowParam(GLFW_OPENED);
  *
  *     // Call our fpsManager to limit the FPS and get the frame duration to pass to the cam->move method
  *     deltaTime = fpsManager.enforceFPS();
  * }
  *
  * That's it! =D
  */

class FpsManager
{

    private:
        double frameStartTime;         // Frame start time
        double frameEndTime;           // Frame end time
        double frameDuration;          // How many milliseconds between the last frame and this frame

        double targetFps;              // The desired FPS to run at (i.e. maxFPS)
        double currentFps;             // The current FPS value
        int    frameCount;             // How many frames have been drawn s

        double targetFrameDuration;    // How many milliseconds each frame should take to hit a target FPS value (i.e. 60fps = 1.0 / 60 = 0.016ms)
        double sleepDuration;          // How long to sleep if we're exceeding the target frame rate duration

        double lastReportTime;         // The timestamp of when we last reported
        double reportInterval;         // How often to update the FPS value

        std::string windowTitle;       // Window title to update view GLFW

        bool verbose;                  // Whether or not to output FPS details to the console or update the window

		ALLEGRO_DISPLAY *display;

        // Private method to set relatively sane defaults. Called by constructors before overwriting with more specific values as required.
        void init(double theTargetFps, ALLEGRO_DISPLAY *display, bool theVerboseSetting){
            setTargetFps(theTargetFps);
			this->display = display;

            frameCount     = 0;
            currentFps     = 0.0;
            sleepDuration  = 0.0;
            frameStartTime = al_get_time();
            frameEndTime   = frameStartTime + 1;
            frameDuration  = 1;
            lastReportTime = frameStartTime;
            reportInterval = 1.0f;
            windowTitle    = "NONE";
            verbose        = theVerboseSetting;
        }

    public:

        // Single parameter constructor - just set a desired framerate and let it go.
        // Note: No FPS reporting by default, although you can turn it on or off later with the setVerbose(true/false) method
        FpsManager(int theTargetFps, ALLEGRO_DISPLAY *display) {
            init(theTargetFps, display, false);
        }

        // Two parameter constructor which sets a desired framerate and a reporting interval in seconds
        FpsManager(int theTargetFps, double theReportInterval, ALLEGRO_DISPLAY *display){
            init(theTargetFps, display, false);

            setReportInterval(theReportInterval);
        }

        // Three parameter constructor which sets a desired framerate, how often to report, and the window title to append the FPS to
        FpsManager(int theTargetFps, float theReportInterval, std::string theWindowTitle, ALLEGRO_DISPLAY *display){
            init(theTargetFps, display, false); // If you specify a window title it's safe to say you want the FPS to update there ;)

            setReportInterval(theReportInterval);

            windowTitle = theWindowTitle;
        }

        // Getter and setter for the verbose property
        bool getVerbose(){
            return verbose;
        }
        void setVerbose(bool theVerboseValue){
            verbose = theVerboseValue;
        }

        // Getter and setter for the targetFps property
        int getTargetFps(){
            return targetFps;
        }

        void setTargetFps(int theFpsLimit){
            // Make at least some attempt to sanitise the target FPS...
            if (theFpsLimit < MIN_TARGET_FPS){
                theFpsLimit = MIN_TARGET_FPS;
                std::cout << "Limiting FPS rate to legal minimum of " << MIN_TARGET_FPS << " frames per second." << std::endl;
            }
            if (theFpsLimit > MAX_TARGET_FPS){
                theFpsLimit = MAX_TARGET_FPS;
                std::cout << "Limiting FPS rate to legal maximum of " << MAX_TARGET_FPS << " frames per second." << std::endl;
            }

            // ...then set it and calculate the target duration of each frame at this framerate
            targetFps = theFpsLimit;
            targetFrameDuration = 1.0 / targetFps;
        }

        double getFrameDuration() { return frameDuration; } // Returns the time it took to complete the last frame in milliseconds

        // Setter for the report interval (how often the FPS is reported) - santises input.
        void setReportInterval(float theReportInterval) {
            // Ensure the time interval between FPS checks is sane (low cap = 0.1s, high-cap = 10.0s)
            // Negative numbers are invalid, 10 fps checks per second at most, 1 every 10 secs at least.
            if (theReportInterval < 0.1) {
                theReportInterval = 0.1;
            }
            if (theReportInterval > 10.0) {
                theReportInterval = 10.0;
            }
            reportInterval = theReportInterval;
        }

        // Method to force our application to stick to a given frame rate and return how long it took to process a frame
        double enforceFPS() {
            // Get the current time
			frameEndTime = al_get_time();

            // Calculate how long it's been since the frameStartTime was set (at the end of this method)
            frameDuration = frameEndTime - frameStartTime;

            if (reportInterval != 0.0f) {

                // Calculate and display the FPS every specified time interval
                if ((frameEndTime - lastReportTime) > reportInterval){
                    // Update the last report time to be now
                    lastReportTime = frameEndTime;

                    // Calculate the FPS as the number of frames divided by the interval in seconds
                    currentFps =  (double)frameCount / reportInterval;

                    // Reset the frame counter to 1 (and not zero - which would make our FPS values off)
                    frameCount = 1;

                    if (verbose){
                        std::cout << "FPS: " << currentFps << std::endl;
                    } // End of if verbose section

					// If the user specified a window title to append the FPS value to...
					if (windowTitle != "NONE"){

						// Append the FPS value to the window title details
						std::string tempWindowTitle = windowTitle + " | " + std::to_string(currentFps) + " FPS";

						// Convert the new window title to a c_str and set it
						const char* pszConstString = tempWindowTitle.c_str();
						al_set_window_title(display, pszConstString);
					}

                } else {// FPS calculation time interval hasn't elapsed yet? Simply increment the FPS frame counter
                    ++frameCount;
                }

            } // End of if we specified a report interval section

            // Calculate how long we should sleep for to stick to our target frame rate
            sleepDuration = targetFrameDuration - frameDuration;

            // If we're running faster than our target duration, sleep until we catch up!
            if (sleepDuration > 0.0)
                al_rest(targetFrameDuration - frameDuration);

            // Reset the frame start time to be now - this means we only need put a single call into the main loop
            frameStartTime = al_get_time();

            // Pass back our total frame duration (including any sleep and the time it took to run this function) to be used as our deltaTime value
            return frameDuration + (frameStartTime - frameEndTime);

        } // End of our enforceFPS method

};

#endif