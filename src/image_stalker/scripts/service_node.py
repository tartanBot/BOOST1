#!/usr/bin/env python

from image_stalker.srv import *
import rospy
import cv2
from std_msgs.msg import String
from sensor_msgs.msg import Image, CompressedImage
from cv_bridge import CvBridge, CvBridgeError


bridge = CvBridge()
def handle_image_process(req):
    #Insert Tanvir's code here
    #define your output image as resp.image_process_resp
    #replace the output of ImageProcessResponse with resp.image_process_resp
    im =cv2.imread('/home/robotanist-gpu/Desktop/images/op'+str(req.image_index)+'.jpg')
    
    return ImageProcessResponse(bridge.cv2_to_imgmsg(im, "bgr8"))

def image_process_server():
    rospy.init_node('image_process')
    s = rospy.Service('image_process', ImageProcess, handle_image_process)
    print "Ready to process the image."
    rospy.spin()

if __name__ == "__main__":
    image_process_server()
