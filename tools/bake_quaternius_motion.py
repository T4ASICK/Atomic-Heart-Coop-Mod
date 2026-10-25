"""Bake selected CC0 Quaternius GLB motion into rig-independent native samples.

Input is the author's Standard in-place GLB. No P3 geometry or reference data
is read or embedded. Generated data retains the author's CC0 provenance.
"""
import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

import numpy as np

NAMES = ['pelvis','spine_01','spine_02','spine_03','neck_01','Head',
         'clavicle_l','upperarm_l','lowerarm_l','hand_l',
         'clavicle_r','upperarm_r','lowerarm_r','hand_r',
         'thigh_l','calf_l','foot_l','ball_l','thigh_r','calf_r','foot_r','ball_r']
CLIPS = [('idle','Idle_Loop'),('walk','Walk_Loop'),('jog','Jog_Fwd_Loop'),
         ('sprint','Sprint_Loop'),('jump_start','Jump_Start'),('jump_loop','Jump_Loop'),('jump_land','Jump_Land')]


def rotate(q):
    x,y,z,w=q/np.linalg.norm(q)
    return np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                     [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                     [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])


def quaternion(r):
    # Symmetric eigenproblem avoids unstable trace branches at 180 degrees.
    x,y,z=r[:,0],r[:,1],r[:,2]
    k=np.array([[x[0]-y[1]-z[2],y[0]+x[1],z[0]+x[2],y[2]-z[1]],
                [y[0]+x[1],y[1]-x[0]-z[2],z[1]+y[2],z[0]-x[2]],
                [z[0]+x[2],z[1]+y[2],z[2]-x[0]-y[1],x[1]-y[0]],
                [y[2]-z[1],z[0]-x[2],x[1]-y[0],x[0]+y[1]+z[2]]])/3
    _,v=np.linalg.eigh(k);q=v[:,-1]
    return q if q[3]>=0 else -q


class GLB:
    def __init__(self,path):
        raw=path.read_bytes();magic,version,size=struct.unpack_from('<III',raw)
        if magic!=0x46546c67 or version!=2 or size!=len(raw):raise ValueError('GLB header')
        n,kind=struct.unpack_from('<II',raw,12)
        if kind!=0x4e4f534a:raise ValueError('GLB JSON chunk')
        self.d=json.loads(raw[20:20+n]);offset=20+n
        length,kind=struct.unpack_from('<II',raw,offset)
        if kind!=0x004e4942:raise ValueError('GLB BIN chunk')
        self.raw=raw[offset+8:offset+8+length];self.cache={}
        self.parents={c:i for i,node in enumerate(self.d['nodes']) for c in node.get('children',[])}
        by_name={n.get('name'):i for i,n in enumerate(self.d['nodes'])}
        self.ids=[by_name[n] for n in NAMES]
        self.basis=np.array([[0,0,1],[-1,0,0],[0,1,0]],dtype=float)

    def array(self,index):
        if index not in self.cache:
            a=self.d['accessors'][index];v=self.d['bufferViews'][a['bufferView']]
            if a['componentType']!=5126 or 'sparse' in a:raise ValueError('Expected dense float animation accessor')
            width={'SCALAR':1,'VEC3':3,'VEC4':4}[a['type']]
            self.cache[index]=np.ndarray((a['count'],width),dtype='<f4',buffer=self.raw,
                offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',width*4),4)).copy()
        return self.cache[index]

    def duration(self,animation):
        return max(float(self.array(s['input'])[-1,0]) for s in animation['samplers'])

    def pose(self,animation,t):
        nodes=[dict(p=np.array(n.get('translation',[0,0,0]),float),q=np.array(n.get('rotation',[0,0,0,1]),float),
                    s=np.array(n.get('scale',[1,1,1]),float)) for n in self.d['nodes']]
        keys={'translation':'p','rotation':'q','scale':'s'}
        for c in animation['channels']:
            sampler=animation['samplers'][c['sampler']]
            if sampler.get('interpolation','LINEAR') not in ('LINEAR','STEP'):raise ValueError('Unsupported interpolation')
            times=self.array(sampler['input'])[:,0];values=self.array(sampler['output'])
            a=max(0,min(len(times)-1,int(np.searchsorted(times,t,side='right'))-1));b=min(a+1,len(times)-1)
            alpha=0 if b==a or sampler.get('interpolation')=='STEP' else np.clip((t-times[a])/(times[b]-times[a]),0,1)
            va,vb=values[a],values[b]
            if c['target']['path']=='rotation' and va@vb<0:vb=-vb
            value=va*(1-alpha)+vb*alpha
            if c['target']['path']=='rotation':value/=np.linalg.norm(value)
            nodes[c['target']['node']][keys[c['target']['path']]]=value
        globals={}
        def world(i):
            if i not in globals:
                n=nodes[i];m=np.eye(4);m[:3,:3]=rotate(n['q'])@np.diag(n['s']);m[:3,3]=n['p']
                globals[i]=world(self.parents[i])@m if i in self.parents else m
            return globals[i]
        positions=[];rotations=[]
        for i in self.ids:
            m=world(i);r=m[:3,:3];r=r/np.linalg.norm(r,axis=0)
            positions.append(self.basis@m[:3,3]);rotations.append(self.basis@r@self.basis.T)
        return np.array(positions),np.array(rotations)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('glb',type=Path);p.add_argument('--out',type=Path,required=True);args=p.parse_args()
    model=GLB(args.glb);animations={a['name']:a for a in model.d['animations']}
    rp,rr=model.pose(animations['A_TPose'],0)
    leg=(np.linalg.norm(rp[15]-rp[14])+np.linalg.norm(rp[16]-rp[15])+np.linalg.norm(rp[19]-rp[18])+np.linalg.norm(rp[20]-rp[19]))/2
    if not .5<leg<1.5:raise ValueError('Source units/proportions unexpected')
    # Ensure the chosen basis agrees with the author's anatomical right/up/forward.
    if rp[11,1]<rp[7,1] or (rp[17]-rp[16])[0]<0:raise ValueError('Source axes unexpected')
    lines=['#pragma once',
        '// CC0 motion by Quaternius. Source and SHA256: SOURCE.json; see LICENSE.txt.',
        'namespace ghost::gait::authored {',
        'struct Frame { std::array<Vec3,22> offset; std::array<Quat,22> rotation; };',
        'struct Clip { const Frame* frames; std::size_t count; float duration; float speed; };']
    def number(x):return f'{float(x):.7f}f'
    lines.append('inline constexpr std::array<Vec3,22> reference{{'+','.join('{'+','.join(map(number,v))+'}' for v in rp/leg)+'}};')
    for key,name in CLIPS:
        animation=animations[name];duration=model.duration(animation);count=max(2,int(round(duration*30)))
        sampled=[model.pose(animation,f*duration/count) for f in range(count)]
        points_all=np.array([sample[0] for sample in sampled])
        velocity=(np.roll(points_all,-1,axis=0)-np.roll(points_all,1,axis=0))/(2*duration/count)
        grounded=np.minimum(points_all[:,[16,20],2]-rp[[16,20],2],points_all[:,[17,21],2]-rp[[17,21],2])<0.015
        speeds=-velocity[:,[16,20],0][grounded]
        speeds=speeds[speeds>0.15]
        speed=float(np.median(speeds)/leg) if len(speeds) else 0.0
        lines.append(f'inline constexpr std::array<Frame,{count}> {key}_frames{{{{')
        for f in range(count):
            points,rs=sampled[f];offset=(points-rp)/leg
            q=[quaternion(r@rest.T) for r,rest in zip(rs,rr)]
            if any(np.max(np.abs(rotate(a)-r@rest.T))>1e-5 for a,r,rest in zip(q,rs,rr)):raise ValueError('Quaternion conversion failed')
            xyz=','.join('{'+','.join(map(number,v))+'}' for v in offset)
            xyzw=','.join('{'+','.join(map(number,v))+'}' for v in q)
            lines.append('    {{{'+xyz+'}},{{'+xyzw+'}}},')
        lines.append('}};');lines.append(f'inline constexpr Clip {key}{{{key}_frames.data(),{count},{number(duration)},{number(speed)}}};')
        print(name,count,'frames',duration,'seconds; normalized speed',speed)
    lines.append('}')
    args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    manifest={'author':'Quaternius','license':'CC0-1.0','source':'https://quaternius.itch.io/universal-animation-library',
              'archive_sha256':'cc73fc4e495b82958207316596317a3f40b9fa38065bde1027937452da537724',
              'glb_sha256':hashlib.sha256(args.glb.read_bytes()).hexdigest(),'clips':dict(CLIPS),
              'source_leg_length_m':leg,'fps':30,'contains_game_assets':False}
    (args.out.parent/'SOURCE.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')


if __name__=='__main__':main()
