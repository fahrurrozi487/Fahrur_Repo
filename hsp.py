import numpy as np
import cv2

# --- GLOBAL VARIABLES ---
# Hold the background frame for background subtraction.
background = None
# Hold the hand's data so all its details are in one place.
hand = None
# Variables to count how many frames have passed and to set the size of the window.
frames_elapsed = 0
FRAME_HEIGHT = 200
FRAME_WIDTH = 300
# Humans come in a ton of beautiful shades and colors.
# Try editing these if your program has trouble recognizing your skin tone.
CALIBRATION_TIME = 30
BG_WEIGHT = 0.5
OBJ_THRESHOLD = 18

# --- CLASSES ---
class HandData:
    top = (0,0)
    bottom = (0,0)
    left = (0,0)
    right = (0,0)
    centerX = 0
    prevCenterX = 0
    isInFrame = False
    isWaving = False
    fingers = None
    gestureList = []
    
    def __init__(self, top, bottom, left, right, centerX):
        self.top = top
        self.bottom = bottom
        self.left = left
        self.right = right
        self.centerX = centerX
        self.prevCenterX = 0
        self.isInFrame = False
        self.isWaving = False
        self.fingers = None
        
    def update(self, top, bottom, left, right):
        self.top = top
        self.bottom = bottom
        self.left = left
        self.right = right
        
    def check_for_waving(self, centerX):
        # We use 'self' for both because prevCenterX will store the old centerX
        self.prevCenterX = self.centerX
        self.centerX = centerX
        
        # Check if the hand moved horizontally significantly
        if abs(self.centerX - self.prevCenterX) > 3:
            self.isWaving = True
        else:
            self.isWaving = False

# --- FUNCTIONS ---
def write_on_image(frame, region_left, region_top, region_right, region_bottom):
    """Write info related to the hand gesture and outline the region of interest."""
    text = "Searching..."

    if frames_elapsed < CALIBRATION_TIME:
        text = f"Calibrating... ({frames_elapsed}/{CALIBRATION_TIME})"
    elif hand is None or hand.isInFrame == False:
        text = "No hand detected"
    else:
        if hand.isWaving:
            text = "Waving"
        elif hand.fingers == 0:
            text = "Rock"
        elif hand.fingers == 1:
            text = "Pointing"
        elif hand.fingers == 2:
            text = "Scissors"
    
    # Draw text with outline
    cv2.putText(frame, text, (10,20), cv2.FONT_HERSHEY_COMPLEX, 0.4,( 0 , 0 , 0 ),2,cv2.LINE_AA)
    cv2.putText(frame, text, (10,20), cv2.FONT_HERSHEY_COMPLEX, 0.4,(255,255,255),1,cv2.LINE_AA)

    # Highlight the region of interest.
    cv2.rectangle(frame, (region_left, region_top), (region_right, region_bottom), (255,255,255), 2)

def get_region(frame, region_top, region_bottom, region_left, region_right):
    """Separate the region of interest and preps it for edge detection"""
    # Separate the region of interest from the rest of the frame.
    region = frame[region_top:region_bottom, region_left:region_right]
    # Make it grayscale so we can detect the edges more easily.
    region = cv2.cvtColor(region, cv2.COLOR_BGR2GRAY)
    # Use a Gaussian blur to prevent frame noise from being labeled as an edge.
    region = cv2.GaussianBlur(region, (5,5), 0)

    return region

def get_average(region):
    """Create a weighted average of the background for image differencing"""
    global background
    # If we haven't captured the background yet, make the current region the background.
    if background is None:
        background = region.copy().astype("float")
        return
    # Otherwise, add this captured frame to the average of the backgrounds.
    cv2.accumulateWeighted(region, background, BG_WEIGHT)

def segment(region):
    """Use image differencing to separate the hand from the background"""
    global hand, background
    # Find the absolute difference between the background and the current frame.
    diff = cv2.absdiff(background.astype(np.uint8), region)

    # Threshold that region with a strict 0 or 1 ruling so only the foreground remains.
    thresholded_region = cv2.threshold(diff, OBJ_THRESHOLD, 255, cv2.THRESH_BINARY)[1]

    # Get the contours of the region, which will return an outline of the hand.
    # Note: OpenCV versions sometimes change the return value of findContours
    contours, _ = cv2.findContours(thresholded_region.copy(), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    # If we didn't get anything, there's no hand.
    if len(contours) == 0:
        if hand is not None:
            hand.isInFrame = False
        return None
    # Otherwise return a tuple of the filled hand (thresholded_region), along with the outline (segmented_region).
    else:
        if hand is not None:
            hand.isInFrame = True
        segmented_region = max(contours, key = cv2.contourArea)
        return (thresholded_region, segmented_region)

def get_hand_data(thresholded_image, segmented_image):
    """Find the extremities of the hand and put them in the global hand object"""
    global hand, frames_elapsed
    
    # Enclose the area around the extremities in a convex hull to connect all outcroppings.
    convexHull = cv2.convexHull(segmented_image)
    
    # Find the extremities for the convex hull and store them as points.
    top    = tuple(convexHull[convexHull[:, :, 1].argmin()][0])
    bottom = tuple(convexHull[convexHull[:, :, 1].argmax()][0])
    left   = tuple(convexHull[convexHull[:, :, 0].argmin()][0])
    right  = tuple(convexHull[convexHull[:, :, 0].argmax()][0])
    
    # Get the center of the palm, so we can check for waving and find the fingers.
    centerX = int((left[0] + right[0]) / 2)
    
    # We put all the info into an object for handy extraction (get it? HANDy?)
    if hand is None:
        hand = HandData(top, bottom, left, right, centerX)
    else:
        hand.update(top, bottom, left, right)
    
    # Only check for waving every 6 frames.
    if frames_elapsed % 6 == 0:
        hand.check_for_waving(centerX)
    
    # We count the number of fingers up every frame, but only change hand.fingers if
    # 12 frames have passed, to prevent erratic gesture counts.
    hand.gestureList.append(count_fingers(thresholded_image))
    if frames_elapsed % 12 == 0:
        hand.fingers = most_frequent(hand.gestureList)
        hand.gestureList.clear()

def count_fingers(thresholded_image):
    """Count the number of fingers using a line intersecting fingertips"""
    
    # Find the height at which we will draw the line to count fingers.
    line_height = int(hand.top[1] + (0.2 * (hand.bottom[1] - hand.top[1])))
    
    # Get the linear region of interest along where the fingers would be.
    # Note: Using np.zeros of dtype=np.uint8 for mask compatibility
    line = np.zeros(thresholded_image.shape[:2], dtype=np.uint8) 
    
    # Draw a line across this region of interest, where the fingers should be.
    # We draw on the 'line' mask, not the thresholded_image
    cv2.line(line, (thresholded_image.shape[1], line_height), (0, line_height), 255, 1)
    
    # Do a bitwise AND to find where the line intersected the hand -- this is where the fingers are.
    line = cv2.bitwise_and(thresholded_image, thresholded_image, mask = line)
    
    # Get the line's new contours.
    contours, _ = cv2.findContours(line.copy(), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    
    fingers = 0
    
    # Count the fingers by making sure the contour lines are "finger-sized", i.e. not too wide.
    for curr in contours:
        width = len(curr)
        
        if width < 3 * abs(hand.right[0] - hand.left[0]) / 4 and width > 5:
            fingers += 1
    
    return fingers

def most_frequent(input_list):
    """Returns the value in a list that appears most frequently"""
    from collections import Counter
    if not input_list:
        return 0
    return Counter(input_list).most_common(1)[0][0]

# --- MAIN EXECUTION BLOCK ---
def main():
    global frames_elapsed, background, hand
    
    # Our region of interest will be the top right part of the frame.
    # These coordinates are based on FRAME_HEIGHT=200 and FRAME_WIDTH=300
    region_top = 0
    region_bottom = int(2 * FRAME_HEIGHT / 3) # 133
    region_left = int(FRAME_WIDTH / 2)       # 150
    region_right = FRAME_WIDTH               # 300

    frames_elapsed = 0
    background = None
    hand = None

    # Try to capture from the default camera (index 0), or index 1 if 0 fails.
    # You might need to change the index (0, 1, 2, ...) depending on your specific webcam setup.
    capture = cv2.VideoCapture(0)
    
    if not capture.isOpened():
        print("Error: Could not open camera index 0. Trying camera index 1...")
        capture = cv2.VideoCapture(1)
        if not capture.isOpened():
            print("Fatal Error: Could not open camera index 0 or 1. Please check your webcam connection or change the index in the code.")
            return

    print("--- Hand Gesture Recognition Started ---")
    print(f"Calibrate the background for {CALIBRATION_TIME} frames by showing only the background.")
    print("Place your hand in the white box after calibration.")
    print("Press 'x' to exit.")

    while (True):
        # Store the frame from the video capture and resize it to the window size.
        ret, frame = capture.read()
        
        if not ret:
            print("Error: Failed to read frame from camera.")
            break
            
        frame = cv2.resize(frame, (FRAME_WIDTH, FRAME_HEIGHT))
        # Flip the frame over the vertical axis so that it works like a mirror.
        frame = cv2.flip(frame, 1)
        
        # Separate the region of interest and prep it for edge detection.
        region = get_region(frame, region_top, region_bottom, region_left, region_right)
        
        if frames_elapsed < CALIBRATION_TIME:
            get_average(region)
            print(f"Calibrating... {frames_elapsed+1}/{CALIBRATION_TIME} frames", end='\r')
        else:
            region_pair = segment(region)
            
            if region_pair is not None:
                # If we have the regions segmented successfully, show them in another window.
                (thresholded_region, segmented_region) = region_pair
                
                # Draw the hand outline on the region of interest window
                display_region = region.copy() # Make a copy to draw on
                cv2.drawContours(display_region, [segmented_region], -1, (255, 255, 255), 2)
                cv2.imshow("Segmented Hand (White box area)", display_region)
                
                get_hand_data(thresholded_region, segmented_region)
        
        # Write the action the hand is doing on the screen, and draw the region of interest.
        write_on_image(frame, region_left, region_top, region_right, region_bottom)
        
        # Show the camera input frame.
        cv2.imshow("Camera Input", frame)
        
        frames_elapsed += 1
        
        # Check if user wants to exit (Press 'x')
        if (cv2.waitKey(1) & 0xFF == ord('x')):
            break

    # When we exit the loop, we have to stop the capture too.
    capture.release()
    cv2.destroyAllWindows()
    print("\n--- Program Exited ---")


if __name__ == "__main__":
    main()
