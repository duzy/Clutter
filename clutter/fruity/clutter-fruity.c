#include <clutter/clutter.h>
#include "clutter-backend-fruity.h"
#include "clutter-stage-fruity.h"
#include "../clutter-main.h"
#include "../clutter-private.h"
#import <UIKit/UIKit.h>

#import  <Foundation/Foundation.h>
#import  <CoreFoundation/CoreFoundation.h>
#include <GraphicsServices/GraphicsServices.h>
#include <OpenGLES/gl.h>
#include <glib.h>

#import  <UIKit/UIView.h>
#import  <UIKit/UITextView.h>
#import  <UIKit/UIHardware.h>
#import  <UIKit/UINavigationBar.h>
#import  <UIKit/UIView-Geometry.h>
#include "clutter-fruity.h"

static EGLDisplay mEGLDisplay;
static EGLContext mEGLContext;
static EGLSurface mEGLSurface;
static CoreSurfaceBufferRef mScreenSurface;
static gboolean alive = TRUE;

@interface StageView : UIView
{
}

@end

static CoreSurfaceBufferRef CreateSurface(int w, int h)
{
  int pitch = w * 2, allocSize = 2 * w * h;
  char *pixelFormat = "565L";
  CFMutableDictionaryRef dict;

  dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(dict, kCoreSurfaceBufferGlobal,        kCFBooleanTrue);
  CFDictionarySetValue(dict, kCoreSurfaceBufferMemoryRegion,  CFSTR("PurpleGFXMem"));
  CFDictionarySetValue(dict, kCoreSurfaceBufferPitch,         CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pitch));
  CFDictionarySetValue(dict, kCoreSurfaceBufferWidth,         CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &w));
  CFDictionarySetValue(dict, kCoreSurfaceBufferHeight,        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &h));
  CFDictionarySetValue(dict, kCoreSurfaceBufferPixelFormat,   CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, pixelFormat));
  CFDictionarySetValue(dict, kCoreSurfaceBufferAllocSize,     CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &allocSize));
  return CoreSurfaceBufferCreate(dict);
}


@implementation StageView

struct GSPathPoint {
  char unk0;
  char unk1;
  short int status;
  int unk2;
  float x;
  float y;
};

typedef struct {
  int unk0;
  int unk1;
  int type;
  int subtype;
  float unk2;
  float unk3;
  float x;
  float y;
  int timestamp1;
  int timestamp2;
  int unk4;
  int modifierFlags;
  int unk5;
  int unk6;
  int mouseEvent;
  short int dx;
  short int fingerCount;
  int unk7;
  int unk8;
  char unk9;
  char numPoints;
  short int unk10;
  struct GSPathPoint points[10];
} MEvent;


- (void)doEvent:(GSEvent*)gs_event
{
  ClutterBackendEGL *ba = CLUTTER_BACKEND_EGL (clutter_get_default_backend());
  int i, j;
  ClutterMainContext  *context;
  ClutterStage *stage = CLUTTER_STAGE_EGL(ba->stage)->wrapper;
  int n_fingers = 0;
  MEvent *event = (MEvent*)gs_event;

  context = clutter_context_get_default ();

  bool mapped[5] = {false, false, false, false, false}; /* an event has been mapped to this device */
  int  evs[5] = {0,0,0,0,0};
  
  for (i = 0; i < event->fingerCount; i++) 
    {
      bool found = false;

      /* NSLog(@"IncomingEvent: %d, pos: %f, %f", i, event->points[i].x, event->points[i].y);*/

      /* check if this finger maps to one of the existing devices */
      for (j = 0; j < 5; j++) 
        {
          ClutterFruityFingerDevice *dev;

          if (mapped[j])
            continue; /* we're already using device j */

          dev = g_slist_nth_data (context->input_devices, j);

          if (!dev->is_down) 
            continue; /* device isn't down we cannot really match against it */

          int dist = ABS(event->points[i].x - dev->x) +
                     ABS(event->points[i].y - dev->y);
          if (dist < 20) 
            {
              found = true;
              mapped[j] = true;

              if (dist >= 1)
                {
                  dev->x = event->points[i].x;
                  dev->y = event->points[i].y;
                  // MOUSEMOVE
                  /*NSLog(@"MouseMove: %d, pos: %d, %d", j, dev->x, dev->y);*/
                  evs[j] = 3;
                }
              break;
            }
        }

      if (!found) 
        {
          ClutterFruityFingerDevice *dev;

          for (j = 0; j < 5 /*n_fingers*/; j++) 
            {
              dev = g_slist_nth_data (context->input_devices, j);
              if (!dev->is_down)
                break;
            }
        
          dev->x = event->points[i].x;
          dev->y = event->points[i].y;
          dev->is_down = TRUE;

          mapped[j] = true;

          // MOUSEDOWN
          /* NSLog(@"MouseDown: %d, pos: %d, %d", j, event->points[i].x, dev->x, dev->y); */
          evs[j] = 2;
        }
    }

  for (j = 0; j < 5; j++) 
    {
      ClutterFruityFingerDevice *dev;

      dev = g_slist_nth_data (context->input_devices, j);

      if (dev->is_down && !mapped[j])
        {
          // MOUSEUP
          /* NSLog(@"MouseUp: %d, pos: %d, %d", j, dev->x, dev->y); */
          evs[j] = 1;
          dev->is_down = FALSE;
        }
    }

  /* Now I guess go through device list and deliver an event for each 
   * if valid and devliver if so...
  */
  {
    i = 0;
    GSList *list_it;

    for (list_it = context->input_devices; 
         list_it != NULL; 
         list_it = list_it->next)
      {
        ClutterFruityFingerDevice *dev = (ClutterFruityFingerDevice *)list_it->data;

        if (evs[i] > 0)
          {
            ClutterEvent *cev;

            if (evs[i] == 1)
              {
                cev = clutter_event_new (CLUTTER_BUTTON_RELEASE);
                cev->button.device = (ClutterInputDevice *)dev;
                cev->button.x = dev->x;
                cev->button.y = dev->y;
                cev->button.button = 1;
                cev->button.time = clutter_get_timestamp () / 1000;
                cev->any.stage = stage;
                clutter_do_event (cev);
                clutter_event_free (cev);
              }
            else if (evs[i] == 2)
              {
                cev = clutter_event_new (CLUTTER_BUTTON_PRESS);
                cev->button.device = (ClutterInputDevice *)dev;
                cev->button.x = dev->x;
                cev->button.y = dev->y;
                cev->button.button = 1;
                cev->button.time = clutter_get_timestamp () / 1000;
                cev->any.stage = stage;
                clutter_do_event (cev);
                clutter_event_free (cev);
              }
            else /* evs = 3, motion */
              {
                cev = clutter_event_new (CLUTTER_MOTION);
                cev->motion.device = (ClutterInputDevice *)dev;
                cev->motion.x = dev->x;
                cev->motion.y = dev->y;
                cev->motion.time = clutter_get_timestamp () / 1000;
                cev->any.stage = stage;
                clutter_do_event (cev);
                clutter_event_free (cev);
              }
          }
        i++;
      }
  }
}

#if 0 // old stylie
- (void) mouseDown:(GSEvent*)event
{
    CGPoint location= GSEventGetLocationInWindow(event);
    ClutterBackendEGL *backend_fruity = CLUTTER_BACKEND_EGL (clutter_get_default_backend());
    ClutterStage *stage = CLUTTER_STAGE_EGL(backend_fruity->stage)->wrapper;
    ClutterEvent *cev;
    float x = location.x;
    float y = location.y;

    cev = clutter_event_new (CLUTTER_BUTTON_PRESS);
    cev->button.x = x;
    cev->button.y = y;
    cev->button.button = 1;
    cev->button.time = clutter_get_timestamp () / 1000;
    cev->any.stage = stage;

    clutter_do_event (cev);
    clutter_event_free (cev);
}

- (void) mouseUp:(GSEvent*)event
{
    ClutterEvent *cev;
    ClutterBackendEGL *backend_fruity = CLUTTER_BACKEND_EGL (clutter_get_default_backend());
    ClutterStage *stage = CLUTTER_STAGE_EGL(backend_fruity->stage)->wrapper;
    CGPoint location= GSEventGetLocationInWindow(event);
    float x = location.x;
    float y = location.y;

    cev = clutter_event_new (CLUTTER_BUTTON_RELEASE);
    cev->button.x = x;
    cev->button.y = y;
    cev->button.button = 1;
    cev->button.time = clutter_get_timestamp () / 1000;
    cev->any.stage = stage;
    clutter_do_event (cev);
    clutter_event_free (cev);
}

- (void) mouseDragged:(GSEvent*)event
{
    ClutterEvent *cev;
    ClutterBackendEGL *backend_fruity = CLUTTER_BACKEND_EGL (clutter_get_default_backend());
    ClutterStage *stage = CLUTTER_STAGE_EGL(backend_fruity->stage)->wrapper;
    CGPoint location= GSEventGetLocationInWindow(event);
    float x = location.x;
    float y = location.y;

    cev = clutter_event_new (CLUTTER_MOTION);
    cev->motion.x = x;
    cev->motion.y = y;
    cev->motion.time = clutter_get_timestamp () / 1000;
    cev->any.stage = stage;
    clutter_do_event (cev);
    clutter_event_free (cev);
}
#endif

/* New... */

- (void)gestureChanged:(GSEvent*)event {
  /*NSLog(@"gestureChanged:");*/
  [self doEvent: event];
}

- (void)gestureEnded:(GSEvent*)event {
  /*NSLog(@"gestureEnded:");*/
  [self doEvent: event];
}

- (void)gestureStarted:(GSEvent*)event {
  /*NSLog(@"gestureStarted:");*/
  [self doEvent: event];
}

- (void)mouseDown:(GSEvent*)event {
  /*NSLog(@"mouseDown:");*/
  [self doEvent: event];
}

- (void)mouseDragged:(GSEvent*)event {
  /*NSLog(@"mouseDragged:");*/
  [self doEvent: event];
}

- (void)mouseEntered:(GSEvent*)event {
  /*NSLog(@"mouseEntered:");*/
  [self doEvent: event];
}

- (void)mouseExited:(GSEvent*)event {
  /*NSLog(@"mouseExited:");*/
  [self doEvent: event];
}

- (void)mouseMoved:(GSEvent*)event {
  /*NSLog(@"mouseMoved:");*/
  [self doEvent: event];
}

- (void)mouseUp:(GSEvent*)event {
  /*NSLog(@"mouseUp:");*/
  [self doEvent: event];
}

- (void)view:(UIView *)view handleTapWithCount:(int)count event:(GSEvent *)event {
  /*NSLog(@"handleTapWithCount: %d", count);*/
  [self doEvent: event];
}

- (double)viewTouchPauseThreshold:(UIView *)view {
  return 0.5;
}

- (BOOL)isFirstResponder {
  return YES;
}

@end


@interface ClutterUIKit : UIApplication
{
  StageView *stage_view;
}
@end

@implementation ClutterUIKit

- (void) applicationDidFinishLaunching: (id) unused
{
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];

    [UIHardware _setStatusBarHeight:0.0f];
    [self setStatusBarMode:2 orientation:0 duration:0.0f fenceID:0];

    CGRect screenRect = [UIHardware fullScreenApplicationContentRect];
    UIWindow* window = [[UIWindow alloc] initWithContentRect: screenRect];

    [window orderFront: self];
    [window makeKey: self];
    [window _setHidden: NO];

    [NSTimer 
        scheduledTimerWithTimeInterval:0.0025
        target: self 
        selector: @selector(update) 
        userInfo: nil
        repeats: YES
    ];

    StageView *stageView = [StageView alloc];
    [stageView initWithFrame: screenRect];
    [window setContentView: stageView];

    stage_view = stageView;

    [pool release];
}

- (void)applicationWillTerminate
{
  /* FIXME: here we should do things to shut down the uikit application */
  [stage_view release];
  ClutterBackendEGL *backend_fruity = CLUTTER_BACKEND_EGL (clutter_get_default_backend());
  ClutterStageEGL   *stage_fruity;
  stage_fruity = CLUTTER_STAGE_EGL(backend_fruity->stage);

  alive = FALSE;
  clutter_actor_unrealize (CLUTTER_ACTOR (stage_fruity));
  clutter_main_quit ();
}

- (void)applicationWillSuspend
{
  ClutterBackendEGL *backend_fruity = CLUTTER_BACKEND_EGL (clutter_get_default_backend());
  ClutterStageEGL   *stage_fruity;
  stage_fruity = CLUTTER_STAGE_EGL(backend_fruity->stage);
  alive = FALSE;
}

- (void)applicationDidResumeFromUnderLock
{
  alive = TRUE;
  [stage_view setNeedsDisplay];
}

- (void) update
{
   if (alive && g_main_context_pending (NULL))
      g_main_context_iteration (NULL, FALSE);
}

@end

void clutter_fruity_main (void)
{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  UIApplicationMain(0, NULL, [ClutterUIKit class]);
  [pool release];
}





