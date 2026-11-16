# Callbacks DAT for an OSC In DAT receiving Pacemaker (ES-02 section 4).
# Writes values into a Constant CHOP called 'pacemaker' (channels tempo, beat, bar, downbeat, confidence).

ROOT = '/pacemaker'
CHOP_NAME = 'pacemaker'

def _chop():
    return op(CHOP_NAME)

def onReceiveOSC(dat, rowIndex, message, bytes, timeStamp, address, args, peer):
    c = _chop()
    if c is None:
        return
    if address == ROOT + '/tempo' and args:
        c.par.value0 = float(args[0])
    elif address == ROOT + '/beat' and len(args) >= 3:
        # args: bar, beat, beatsPerBar
        c.par.value2 = int(args[0])
        c.par.value1 = int(args[1])
    elif address == ROOT + '/downbeat' and args:
        c.par.value3 = int(args[0])          # bar number of the last downbeat
        # a pulse for a Trigger CHOP / Count CHOP: bump a counter stored on the component
        parent().store('downbeatCount', parent().fetch('downbeatCount', 0) + 1)
    elif address == ROOT + '/confidence' and args:
        c.par.value4 = float(args[0])
    elif address == ROOT + '/state' and args:
        parent().store('pacemakerState', str(args[0]))
    return
