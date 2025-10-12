#!/usr/bin/python3

#
# This script monitor battery via ic2 and keyboard events.
#
# Additional comments are found in each function below
#
#


from evdev import InputDevice, categorize, ecodes, list_devices
from select import select


import subprocess


import sys
import os
import time

from threading import Thread
from queue import Queue


UPS_LOGFILE="/dev/shm/upslog.txt"
KEYBOARD_LOCKFILE="/dev/shm/argononeupkeyboardlock.txt"

###################
# Utilty Functions
###################

# Debug Logger
def debuglog(typestr, logstr):
	try:
		DEBUGFILE="/dev/shm/argononeupkeyboarddebuglog.txt"
		tmpstrpadding = "                      "

		with open(DEBUGFILE, "a") as txt_file:
			txt_file.write("["+time.asctime(time.localtime(time.time()))+"] "+typestr.upper()+" "+logstr.strip().replace("\n","\n"+tmpstrpadding)+"\n")
	except:
		pass

def runcmdlist(key, cmdlist):
	try:
		result = subprocess.run(cmdlist,
			capture_output=True,
			text=True,
			check=True
		)
		#debuglog(key+"-result-output",str(result.stdout))
		if result.stderr:
			debuglog(key+"-result-error",str(result.stderr))
		#debuglog(key+"-result-code",str(result.returncode))

	except subprocess.CalledProcessError as e:
		debuglog(key+"-error-output",str(e.stdout))
		if result.stderr:
			debuglog(key+"-error-error",str(e.stderr))
		debuglog(key+"-error-code",str(e.returncode))
	except FileNotFoundError:
		debuglog(key+"-error-filenotfound","Command Not Found")
	except Exception as othererr:
		try:
			debuglog(key+"-error-other", str(othererr))
		except:
			debuglog(key+"-error-other", "Other Error")

def createlockfile(fname):
	try:
		if os.path.isfile(fname):
			return True
	except Exception as checklockerror:
		try:
			debuglog("keyboard-lock-error", str(checklockerror))
		except:
			debuglog("keyboard-lock-error", "Error Checking Lock File")
	try:
		with open(fname, "w") as txt_file:
			txt_file.write(time.asctime(time.localtime(time.time()))+"\n")
	except Exception as lockerror:
		try:
			debuglog("keyboard-lock-error", str(lockerror))
		except:
			debuglog("keyboard-lock-error", "Error Creating Lock File")
	return False

def deletelockfile(fname):
	try:
		os.remove(fname)
	except Exception as lockerror:
		try:
			debuglog("keyboard-lock-error", str(lockerror))
		except:
			debuglog("keyboard-lock-error", "Error Removing Lock File")


# System Notifcation
def notifymessage(message, iscritical):
	if not isinstance(message, str) or len(message.strip()) == 0:
		return

	wftype="notify"
	if iscritical:
		wftype="critical"
	os.system("export SUDO_UID=1000; wfpanelctl "+wftype+" \""+message+"\"")
	os.system("export DISPLAY=:0.0; lxpanelctl notify \""+message+"\"")


#############
# Battery (copied)
#############

def battery_loadlogdata():
	# status, version, time, schedule
	outobj = {}
	try:
		fp = open(UPS_LOGFILE, "r")
		logdata = fp.read()
		alllines = logdata.split("\n")
		ctr = 0
		while ctr < len(alllines):
			tmpval = alllines[ctr].strip()
			curinfo = tmpval.split(":")
			if len(curinfo) > 1:
				tmpattrib = curinfo[0].lower().split(" ")
				# The rest are assumed to be value
				outobj[tmpattrib[0]] = tmpval[(len(curinfo[0])+1):].strip()
			ctr = ctr + 1
	except Exception as einit:
		try:
			debuglog("keyboard-battery-error", str(einit))
		except:
			debuglog("keyboard-battery-error", "Error getting battery status")
		#pass

	return outobj


def keyboardevent_getdevicepaths():
	outlist = []
	try:
		for path in list_devices():
			try:
				tmpdevice = InputDevice(path)
				keyeventlist = tmpdevice.capabilities().get(ecodes.EV_KEY, [])
				# Keyboard has EV_KEY (key) and EV_REP (autorepeat)
				if ecodes.KEY_BRIGHTNESSDOWN in keyeventlist and ecodes.KEY_BRIGHTNESSDOWN in keyeventlist:
					outlist.append(path)
				elif ecodes.KEY_F2 in keyeventlist and ecodes.KEY_F3 in keyeventlist:
					# Keyboards with FN key sometimes do not include KEY_BRIGHTNESS in declaration
					outlist.append(path)
				tmpdevice.close()
			except:
				pass
	except:
		pass
	return outlist

def keyboardevent_devicechanged(curlist, newlist):
	try:
		for curpath in curlist:
			if curpath not in newlist:
				return True
		for newpath in newlist:
			if newpath not in curlist:
				return True
	except:
		pass
	return False

def keyboardevent_getbrigthnessinfo(defaultlevel=50):
	level = defaultlevel
	try:
		# VCP code x10(Brightness       ): current value = 90, max value = 100
		output = subprocess.check_output(["ddcutil", "getvcp", "10"], text=True, stderr=subprocess.DEVNULL)
		debuglog("keyboard-brightness-info", output)
		level = int(output.split(":")[-1].split(",")[0].split("=")[-1].strip())
	except Exception as einit:
		try:
			debuglog("keyboard-brightness-error", str(einit))
		except:
			debuglog("keyboard-brightness-error", "Error getting base value")


	return {
			"level": level
		}


def keyboardevent_adjustbrigthness(baselevel, adjustval=5):
	curlevel = baselevel
	if adjustval == 0:
		return {
			"level": baselevel
		}

	try:
		tmpobj = keyboardevent_getbrigthnessinfo(curlevel)
		curlevel = tmpobj["level"]
	except Exception:
		pass

	tmpval = max(10, min(100, curlevel + adjustval))
	if tmpval != curlevel:
		try:
			debuglog("keyboard-brightness", str(curlevel)+"% to "+str(tmpval)+"%")
			runcmdlist("brightness", ["ddcutil", "setvcp", "10", str(tmpval)])
			notifymessage("Brightness: "+str(tmpval)+"%", False)
		except Exception as adjusterr:
			try:
				debuglog("keyboard-brightness-error", str(adjusterr))
			except:
				debuglog("keyboard-brightness-error", "Error adjusting value")
			return {
					"level": curlevel
				}

	# DEBUG: Checking
	#keyboardevent_getbrigthnessinfo(tmpval)
	return {
			"level": tmpval
		}


def keyboardevent_getvolumesinkid(usedefault=True):
	if usedefault == True:
		return "@DEFAULT_SINK@"
	cursinkid = 0
	try:
		output = subprocess.check_output(["wpctl", "status"], text=True, encoding='utf-8', stderr=subprocess.DEVNULL)

		# Find Audio section
		tmpline = ""
		foundidx = 0
		lines = output.splitlines()
		lineidx = 0
		while lineidx < len(lines):
			tmpline = lines[lineidx].strip()
			if tmpline == "Audio":
				foundidx = lineidx
				break
			lineidx = lineidx + 1

		if foundidx < 1:
			return 0

		# Find Sinks section
		foundidx = 0
		lineidx = lineidx + 1
		while lineidx < len(lines):
			if "Sinks:" in lines[lineidx]:
				foundidx = lineidx
				break
			elif len(lines[lineidx]) < 1:
				break
			lineidx = lineidx + 1

		if foundidx < 1:
			return 0

		# Get find default id, or first id
		lineidx = lineidx + 1
		while lineidx < len(lines):
			if "vol:" in lines[lineidx] and "." in lines[lineidx]:
				tmpstr = lines[lineidx].split(".")[0]
				tmplist = tmpstr.split()
				if len(tmplist) > 1:
					if tmplist[len(tmplist)-2] == "*":
						return int(tmplist[len(tmplist)-1])
				if len(tmplist) > 0 and cursinkid < 1:
					cursinkid = int(tmplist[len(tmplist)-1])
			elif len(lines[lineidx]) < 3:
				break
			lineidx = lineidx + 1
	except Exception as einit:
		try:
			debuglog("keyboard-volume-error", str(einit))
		except:
			debuglog("keyboard-volume-error", "Error getting device ID")

	return cursinkid


def keyboardevent_getvolumeinfo(deviceidstr="", defaultlevel=50, defaultmuted=0):
	muted = defaultmuted
	level = defaultlevel
	try:
		if deviceidstr == "":
			audioidstr = str(keyboardevent_getvolumesinkid())
			if audioidstr == "0":
				debuglog("keyboard-volume-error", "Error getting device id")
				return {
						"level": defaultmuted,
						"muted": defaultlevel
					}

			deviceidstr = audioidstr

		output = subprocess.check_output(["wpctl", "get-volume", deviceidstr], text=True, stderr=subprocess.DEVNULL)
		debuglog("keyboard-volume-info", output)

		muted = 0
		level = 0
		# Parse output, examples
		# 	Volume: 0.65
		# 	Volume: 0.55 [MUTED]
		outlist = output.split()
		if len(outlist) > 0:
			# Get last element
			tmpstr = outlist[len(outlist)-1]
			# Check if muted
			if "MUTE" in tmpstr:
				muted = 1
				if len(outlist) > 1:
					tmpstr = outlist[len(outlist)-2]
			if tmpstr.endswith("%"):
				# Level 100% to 0%
				level = int(float(tmpstr[:-1]))
			elif tmpstr.replace('.', '').isdigit():
				# Level 1.00 to 0.00
				level = int(float(tmpstr) * 100.0)
	except Exception as einit:
		try:
			debuglog("keyboard-volume-error", str(einit))
		except:
			debuglog("keyboard-volume-error", "Error getting base value")
		return {
				"level": defaultmuted,
				"muted": defaultlevel
			}

	#debuglog("keyboard-volume-get", str(level)+"% Mute:"+str(muted))

	return {
			"level": level,
			"muted": muted
		}


def keyboardevent_adjustvolume(baselevel, basemuted, adjustval=5):
	curlevel = baselevel
	curmuted = basemuted
	needsnotification = False

	deviceidstr = str(keyboardevent_getvolumesinkid())
	if deviceidstr == "0":
		debuglog("keyboard-volume-error", "Error getting device id")
		return {
				"level": baselevel,
				"muted": basemuted
			}

	try:
		tmpobj = keyboardevent_getvolumeinfo(deviceidstr, curlevel, curmuted)
		curlevel = tmpobj["level"]
		curmuted = tmpobj["muted"]
	except Exception:
		pass

	tmpmuted = curmuted
	if adjustval == 0:
		# Toggle Mute
		if curmuted == 0:
			tmpmuted = 1
		else:
			tmpmuted = 0

	tmpval = max(10, min(100, curlevel + adjustval))
	if tmpval != curlevel:
		try:
			debuglog("keyboard-volume", str(curlevel)+"% to "+str(tmpval)+"%")
			runcmdlist("volume", ["wpctl", "set-volume", deviceidstr, f"{tmpval}%"])
			needsnotification = True
			tmpmuted = 0
		except Exception as adjusterr:
			try:
				debuglog("keyboard-volume-error", str(adjusterr))
			except:
				debuglog("keyboard-volume-error", "Error adjusting value")
			return {
					"level": curlevel,
					"muted": curmuted
				}
	elif adjustval != 0:
		# To unmute even if no volume level change
		tmpmuted = 0

	if tmpmuted != curmuted:
		try:
			debuglog("keyboard-mute", str(tmpmuted))
			runcmdlist("mute", ["wpctl", "set-mute", deviceidstr, str(tmpmuted)])
			needsnotification = True
		except Exception as adjusterr:
			try:
				debuglog("keyboard-mute-error", str(adjusterr))
			except:
				debuglog("keyboard-mute-error", "Error adjusting value")
			return {
					"level": tmpval,
					"muted": curmuted
				}
	#if tmpmuted == 1:
	#	notifymessage("Volume: Muted", False)
	#else:
	#	notifymessage("Volume: "+str(tmpval)+"%", False)

	# DEBUG: Checking
	#keyboardevent_getvolumeinfo(deviceidstr, tmpval, tmpmuted)

	return {
			"level": tmpval,
			"muted": tmpmuted
		}





def keyboardevent_check(readq):
	ADJUSTTYPE_NONE=0
	ADJUSTTYPE_BRIGHTNESS=1
	ADJUSTTYPE_VOLUME=2
	ADJUSTTYPE_MUTE=3
	ADJUSTTYPE_BATTERYINFO=4

	READTIMEOUTSECS = 1.0

	PRESSWAITINTERVALSEC = 2
	HOLDWAITINTERVALSEC = 1
	while True:
		try:
			keypresstimestamp = {}
			keyholdtimestamp = {}
			# Get Devices
			devicelist = []
			devicefdlist = []
			devicepathlist = keyboardevent_getdevicepaths()

			deviceidx = 0
			while deviceidx < len(devicepathlist):
				try:
					tmpdevice = InputDevice(devicepathlist[deviceidx])
					devicelist.append(tmpdevice)
					devicefdlist.append(tmpdevice.fd)
				except Exception as deverr:
					try:
						debuglog("keyboard-deviceerror-", str(deverr)+ " "+ devicepathlist[deviceidx])
					except:
						debuglog("keyboard-deviceerror", "Error "+devicepathlist[deviceidx])
				deviceidx = deviceidx + 1


			# Get current levels
			curvolumemuted = 0
			curvolume = 50

			curbrightness = 50

			try:
				tmpobj = keyboardevent_getbrigthnessinfo()
				curbrightness = tmpobj["level"]
			except Exception:
				pass

			try:
				tmpobj = keyboardevent_getvolumeinfo()
				curvolumemuted = tmpobj["muted"]
				curvolume = tmpobj["level"]
			except Exception:
				pass

			try:
				debuglog("keyboard-update", str(len(devicefdlist))+" Devices")
				while len(devicefdlist) > 0:
					# Exception when one of the devices gets removed
					# Wait for events on any registered device
					r, w, x = select(devicefdlist, [], [], READTIMEOUTSECS)
					for fd in r:
						found = False
						deviceidx = 0
						while deviceidx < len(devicefdlist):
							if devicefdlist[deviceidx] == fd:
								found = True
								break
							deviceidx = deviceidx + 1
						if found:
							for event in devicelist[deviceidx].read():
								try:
									# Process the event
									#print("Device: "+devicelist[deviceidx].path+", Event: ", event)
									#debuglog("keyboard-event", "Device: "+devicelist[deviceidx].path+", Event: "+str(event))
									if event.type == ecodes.EV_KEY:
										key_event = categorize(event)
										keycodelist = []
										if event.value == 1 or event.value == 0:
											#debuglog("keyboard-event", "Mode:"+str(event.value)+" Key Code: "+str(key_event.keycode))

											if isinstance(key_event.keycode, str):
												keycodelist = [key_event.keycode]
											else:
												keycodelist = key_event.keycode

										keycodelistidx = 0
										while keycodelistidx < len(keycodelist):
											tmpkeycode = keycodelist[keycodelistidx]
											keycodelistidx = keycodelistidx + 1
											if tmpkeycode not in ["KEY_BRIGHTNESSDOWN", "KEY_BRIGHTNESSUP", "KEY_VOLUMEDOWN", "KEY_VOLUMEUP"]:
												if event.value == 0:
													# Hold for unhandled keys
													continue
												elif tmpkeycode not in ["KEY_PAUSE", "KEY_MUTE"]:
													# Press for unhandled keys
													continue

											adjustval = 0
											adjusttype = ADJUSTTYPE_NONE

											if event.value == 1:
												# Press
												debuglog("keyboard-event", "Press Key Code: "+str(tmpkeycode))

												tmptime = time.time()
												# Guard time for press
												if tmpkeycode not in ["KEY_MUTE"]:
													# Buttons w/o guard time
													keypresstimestamp[tmpkeycode] = tmptime
												elif tmpkeycode in keypresstimestamp:
													if (tmptime - keypresstimestamp[tmpkeycode]) >= PRESSWAITINTERVALSEC:
														keypresstimestamp[tmpkeycode] = tmptime
													else:
														continue
												else:
													# First Press
													keypresstimestamp[tmpkeycode] = tmptime

												if tmpkeycode == "KEY_BRIGHTNESSDOWN" or tmpkeycode == "KEY_BRIGHTNESSUP":
													adjusttype = ADJUSTTYPE_BRIGHTNESS
													if tmpkeycode == "KEY_BRIGHTNESSDOWN":
														adjustval = -5
													else:
														adjustval = 5
												elif tmpkeycode == "KEY_VOLUMEDOWN" or tmpkeycode == "KEY_VOLUMEUP":
													adjusttype = ADJUSTTYPE_VOLUME
													if tmpkeycode == "KEY_VOLUMEDOWN":
														adjustval = -5
													else:
														adjustval = 5
												elif tmpkeycode == "KEY_PAUSE":
													adjusttype = ADJUSTTYPE_BATTERYINFO
												elif tmpkeycode == "KEY_MUTE":
													adjusttype = ADJUSTTYPE_MUTE
													adjustval = 0

											elif event.value == 0:
												# Hold
												debuglog("keyboard-event", "Hold Key Code: "+str(tmpkeycode))

												tmptime = time.time()
												if tmpkeycode in keypresstimestamp:
													# Guard time before first for hold
													if (tmptime - keypresstimestamp[tmpkeycode]) >= PRESSWAITINTERVALSEC:
														# Guard time for hold
														if tmpkeycode in keyholdtimestamp:
															if (tmptime - keyholdtimestamp[tmpkeycode]) >= HOLDWAITINTERVALSEC:
																keyholdtimestamp[tmpkeycode] = tmptime
															else:
																continue
														else:
															# First Hold event
															keyholdtimestamp[tmpkeycode] = tmptime
													else:
														continue
												else:
													# Should not happen, but treat as if first press
													keypresstimestamp[tmpkeycode] = tmptime

												if tmpkeycode == "KEY_BRIGHTNESSDOWN" or tmpkeycode == "KEY_BRIGHTNESSUP":
													adjusttype = ADJUSTTYPE_BRIGHTNESS
													if tmpkeycode == "KEY_BRIGHTNESSDOWN":
														adjustval = -10
													else:
														adjustval = 10
												elif tmpkeycode == "KEY_VOLUMEDOWN" or tmpkeycode == "KEY_VOLUMEUP":
													adjusttype = ADJUSTTYPE_VOLUME
													if tmpkeycode == "KEY_VOLUMEDOWN":
														adjustval = -10
													else:
														adjustval = 10



											tmplockfilea = KEYBOARD_LOCKFILE+".a"
											if createlockfile(tmplockfilea) == False:
												# Debug ONLY
												if event.value == 1:
													debuglog("keyboard-event", "Press Key Code: "+str(tmpkeycode)+ " => "+str(adjusttype))
												else:
													debuglog("keyboard-event", "Hold Key Code: "+str(tmpkeycode)+ " => "+str(adjusttype))

												if adjusttype == ADJUSTTYPE_BRIGHTNESS:
													try:
														tmpobj = keyboardevent_adjustbrigthness(curbrightness, adjustval)
														curbrightness = tmpobj["level"]
													except Exception:
														pass
												elif adjusttype == ADJUSTTYPE_VOLUME or adjusttype == ADJUSTTYPE_MUTE:
													try:
														tmpobj = keyboardevent_adjustvolume(curvolume, curvolumemuted, adjustval)
														curvolumemuted = tmpobj["muted"]
														curvolume = tmpobj["level"]
													except Exception:
														pass
												elif adjusttype == ADJUSTTYPE_BATTERYINFO:
													outobj = battery_loadlogdata()
													try:
														notifymessage(outobj["power"], False)
													except:
														pass
												deletelockfile(tmplockfilea)

								except Exception as keyhandleerr:
									try:
										debuglog("keyboard-keyerror", str(keyhandleerr))
									except:
										debuglog("keyboard-keyerror", "Error")

					else:
						# No events received within the timeout
						pass
					newpathlist = keyboardevent_getdevicepaths()
					if keyboardevent_devicechanged(devicepathlist, newpathlist):
						debuglog("keyboard-update", "Device list changed")
						break

			except Exception as e:
				try:
					debuglog("keyboard-mainerror", str(e))
				except:
					debuglog("keyboard-mainerror", "Error")

			# Close devices
			while len(devicelist) > 0:
				tmpdevice = devicelist.pop(0)
				try:
					tmpdevice.close()
				except:
					pass

		except Exception as mainerr:
			time.sleep(10)
		# While True


if len(sys.argv) > 1:
	cmd = sys.argv[1].upper()
	if cmd == "SERVICE":
		if createlockfile(KEYBOARD_LOCKFILE) == True:
			debuglog("keyboard-service", "Already running")
		else:
			try:
				debuglog("keyboard-service", "Service Starting")
				keyboardevent_check("")
			except Exception as einit:
				try:
					debuglog("keyboard-service-error", str(einit))
				except:
					debuglog("keyboard-service-error", "Error")
			checklockerror(KEYBOARD_LOCKFILE)
			debuglog("keyboard-service", "Service Stopped")
