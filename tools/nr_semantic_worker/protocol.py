"""Local child pipe framing. Explicit JSON fields, bounded pixels, no file requests."""
import json, struct

def read_exact(stream, size):
    if type(size) is not int or not 0<=size<=16*1024*1024: raise ValueError('unbounded read')
    data=bytearray()
    while len(data)<size:
        part=stream.read(size-len(data))
        if not part: raise EOFError('truncated child pipe')
        data.extend(part)
    return bytes(data)

def _object(pairs):
    result={}
    for k,v in pairs:
        if k in result: raise ValueError('duplicate JSON field')
        result[k]=v
    return result

def read_message(stream, cap=65536):
    n=struct.unpack('<I',read_exact(stream,4))[0]
    if not 0<n<=cap: raise ValueError('metadata exceeds bound')
    value=json.loads(read_exact(stream,n).decode('utf-8'),object_pairs_hook=_object,
                     parse_constant=lambda x: (_ for _ in ()).throw(ValueError('non-finite JSON')))
    if not isinstance(value,dict): raise ValueError('expected metadata object')
    return value

def write_message(stream,value,cap=1024*1024):
    raw=json.dumps(value,allow_nan=False,separators=(',',':')).encode('utf-8')
    if not 0<len(raw)<=cap: raise ValueError('metadata exceeds bound')
    stream.write(struct.pack('<I',len(raw))+raw);stream.flush()

def validate_request(r,nonce):
    fields={'schema','nonce','sequence','epoch','capture_ns','width','height','stride','bytes','maximum_persons','pose_requested','detect_objects'}
    if r.get('schema')==3:fields.add('hold_ms')
    if set(r)!=fields or type(r['schema']) is not int or r['schema'] not in (2,3) or r['nonce']!=nonce:
        raise ValueError('wrong schema or host session')
    if r['schema']==3 and (type(r['hold_ms']) is not int or not 0<=r['hold_ms']<=2000):raise ValueError('invalid hold_ms')
    if type(r['pose_requested']) is not bool or type(r['detect_objects']) is not bool: raise ValueError('invalid detection mode')
    for k,lo,hi in [('sequence',1,2**64-1),('capture_ns',1,2**64-1),('width',1,2048),
                     ('height',1,2048),('stride',4,8192),('bytes',4,16*1024*1024),('maximum_persons',1,16)]:
        if type(r[k]) is not int or not lo<=r[k]<=hi: raise ValueError('invalid '+k)
    if not isinstance(r['epoch'],list) or len(r['epoch'])!=8 or any(type(v) is not int or not 0<v<2**64 for v in r['epoch']):
        raise ValueError('invalid epoch')
    if r['stride']<r['width']*4 or r['bytes']!=r['stride']*r['height']: raise ValueError('invalid pixel extent')
    return r
