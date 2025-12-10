import RPi.GPIO as GPIO
import time

WAIT_TIME = 1

class GetCaseFanSpeed:
    TACH = 18
    PULSE = 2
    def __init__( self, tachPin = TACH):
        self._tachPin = tachPin
        self._rpm = 0
        self._t   = time.time()
        self.GPIO = GPIO
        self.GPIO.setmode( self.GPIO.BCM )
        self.GPIO.setwarnings( False )
        self.GPIO.setup( self._tachPin, self.GPIO.IN, pull_up_down=self.GPIO.PUD_UP )
        self.GPIO.add_event_detect( self._tachPin, self.GPIO.FALLING, self._calcRPM )
        
    def __del__( self ):
        self.GPIO.cleanup()
        
    def _calcRPM( self, n ):
        dt = time.time() - self._t
        if dt < 0.005: return # Reject spuriously short pulses
        
        freq = 1 / dt
        self._rpm = (freq / GetCaseFanSpeed.PULSE) * 60
        self._t = time.time()
    
    @property
    def RPM( self ):
        return self._rpm
    
if __name__ == "__main__":
    
    fanSpeed = GetCaseFanSpeed()

    try:
        for i in range( 10 ):
            print( f"{fanSpeed.RPM:.0f} RPM" )
            time.sleep( 2 )
        
    except KeyboardInterrupt:
        GPIO.cleanup()
    
    