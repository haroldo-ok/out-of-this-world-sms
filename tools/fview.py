import sys, numpy as np
from PIL import Image
REC=4+48+64000
def read(fn, i):
    with open(fn,'rb') as f:
        f.seek(i*REC); b=f.read(REC)
    t=int.from_bytes(b[:4],'little'); pal=np.frombuffer(b[4:52],np.uint8).reshape(16,3)
    idx=np.frombuffer(b[52:],np.uint8).reshape(200,320)
    return t,pal,idx
if __name__=='__main__':
    fn=sys.argv[1]; out=sys.argv[2]; ids=[int(x) for x in sys.argv[3:]]
    tiles=[]
    for i in ids:
        t,pal,idx=read(fn,i); tiles.append(pal[idx])
    cols=4; rows=(len(tiles)+cols-1)//cols
    sheet=np.zeros((rows*204,cols*324,3),np.uint8)
    for k,im in enumerate(tiles):
        r,c=divmod(k,cols); sheet[r*204:r*204+200,c*324:c*324+320]=im
    Image.fromarray(sheet).save(out)
