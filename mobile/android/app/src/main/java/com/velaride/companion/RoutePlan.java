package com.velaride.companion;

import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import javax.xml.parsers.DocumentBuilderFactory;
import org.w3c.dom.*;
import org.xml.sax.InputSource;
import org.xml.sax.SAXException;

/** Ordered GPX route-point guidance; no road routing or map inference. */
public final class RoutePlan {
    public static final int MAX_BYTES=512*1024, MAX_POINTS=2000;
    public static final class Point {
        public final double lat,lon;
        public Point(double lat,double lon) {
            if(!Double.isFinite(lat)||!Double.isFinite(lon)||lat < -90||lat > 90||lon < -180||lon > 180)
                throw new IllegalArgumentException("路线坐标异常");
            this.lat=lat;this.lon=lon;
        }
    }
    public static final class Hint {
        public final int direction,index,total;
        public final long distanceM;
        public final boolean offRoute;
        Hint(int direction,long distanceM,boolean offRoute,int index,int total) {
            this.direction=direction;this.distanceM=distanceM;this.offRoute=offRoute;this.index=index;this.total=total;
        }
        public String text() {
            String[] names={"直行","左转","右转","折返","已到终点","前往起点"};
            return names[direction]+" · 距路线点 "+distanceM+" 米 · "+index+"/"+total
                    +(offRoute?"\n已偏离路线，请停车核对。":"");
        }
    }
    private final List<Point> points;
    private int next;
    private boolean started,arrived;
    public RoutePlan(List<Point> source) {
        if(source==null||source.size()<2||source.size()>MAX_POINTS) throw new IllegalArgumentException("路线需要 2–2000 个点");
        List<Point> kept=new ArrayList<>();
        for(int i=0;i<source.size();i++) {
            Point point=source.get(i);
            if(point==null) throw new IllegalArgumentException("路线点缺失");
            if(kept.isEmpty()||distance(kept.get(kept.size()-1),point)>=25||i==source.size()-1) {
                if(!kept.isEmpty() && distance(kept.get(kept.size()-1),point)<0.1) continue;
                kept.add(point);
            }
        }
        if(kept.size()<2) throw new IllegalArgumentException("路线没有有效长度");
        points=Collections.unmodifiableList(kept);
    }
    public static RoutePlan parse(byte[] bytes) throws Exception {
        if(bytes.length==0||bytes.length>MAX_BYTES) throw new IOException("GPX 文件为空或超过 512 KB");
        String xml=new String(bytes,StandardCharsets.UTF_8);
        String upper=xml.toUpperCase(Locale.ROOT);
        if(xml.indexOf('\0')>=0||upper.contains("<!DOCTYPE")||upper.contains("<!ENTITY"))
            throw new IOException("请使用无 DTD/实体声明的 UTF-8 GPX 文件");
        DocumentBuilderFactory factory=DocumentBuilderFactory.newInstance();factory.setNamespaceAware(true);
        javax.xml.parsers.DocumentBuilder builder=factory.newDocumentBuilder();
        builder.setEntityResolver((publicId,systemId)->{throw new SAXException("外部实体已禁用");});
        Document document=builder.parse(new InputSource(new StringReader(xml)));
        if(!"gpx".equals(document.getDocumentElement().getLocalName())) throw new IOException("不是 GPX 文件");
        if(document.getElementsByTagNameNS("*","rte").getLength()>1 ||
                document.getElementsByTagNameNS("*","trkseg").getLength()>1)
            throw new IOException("请导入单条路线或单段轨迹，避免跨段连接");
        NodeList nodes=document.getElementsByTagNameNS("*","rtept");
        if(nodes.getLength()==0) nodes=document.getElementsByTagNameNS("*","trkpt");
        if(nodes.getLength()<2||nodes.getLength()>MAX_POINTS) throw new IOException("GPX 路线需包含 2–2000 个有序路线点");
        List<Point> points=new ArrayList<>();
        for(int i=0;i<nodes.getLength();i++) {
            Element e=(Element)nodes.item(i);
            points.add(new Point(Double.parseDouble(e.getAttribute("lat")),Double.parseDouble(e.getAttribute("lon"))));
        }
        return new RoutePlan(points);
    }
    public void restart() { next=0;started=false;arrived=false; }
    public int size() { return points.size(); }
    public Hint update(double lat,double lon,double accuracy) {
        Point current=new Point(lat,lon);
        if(!Double.isFinite(accuracy)||accuracy<0||accuracy>50) throw new IllegalArgumentException("定位精度不足");
        double radius=Math.max(20,Math.min(50,accuracy*1.5));
        while(!arrived && distance(current,points.get(next))<=radius) {
            started=true;
            if(next==points.size()-1) {arrived=true;break;}
            next++;
        }
        boolean off=false;
        if(started&&!arrived) {
            double nearest=Double.MAX_VALUE;
            for(int i=Math.max(0,next-1);i<points.size()-1;i++)
                nearest=Math.min(nearest,segmentDistance(current,points.get(i),points.get(i+1)));
            off=nearest>Math.max(80,accuracy*3);
        }
        int direction=0;
        if(arrived) direction=4;
        else if(!started) direction=5;
        else if(next<points.size()-1) {
            double change=(bearing(points.get(next),points.get(next+1))-bearing(points.get(next-1),points.get(next))+540)%360-180;
            direction=Math.abs(change)>135?3:change>35?2:change < -35?1:0;
        }
        long metres=arrived?0:Math.round(distance(current,points.get(next)));
        if(metres>1000000) throw new IllegalArgumentException("距路线点超过 1000 公里，请核对导入路线");
        return new Hint(direction,metres,off,next+1,points.size());
    }
    private static double distance(Point a,Point b) {
        double lat=Math.toRadians(b.lat-a.lat),lon=Math.toRadians(b.lon-a.lon);
        double h=Math.sin(lat/2)*Math.sin(lat/2)+Math.cos(Math.toRadians(a.lat))*Math.cos(Math.toRadians(b.lat))*Math.sin(lon/2)*Math.sin(lon/2);
        return 6371000*2*Math.atan2(Math.sqrt(h),Math.sqrt(Math.max(0,1-h)));
    }
    private static double bearing(Point a,Point b) {
        double la=Math.toRadians(a.lat),lb=Math.toRadians(b.lat),dl=Math.toRadians(b.lon-a.lon);
        return Math.toDegrees(Math.atan2(Math.sin(dl)*Math.cos(lb),Math.cos(la)*Math.sin(lb)-Math.sin(la)*Math.cos(lb)*Math.cos(dl)));
    }
    private static double segmentDistance(Point p,Point a,Point b) {
        double scale=6371000*Math.PI/180,cos=Math.cos(Math.toRadians(p.lat));
        double ax=(a.lon-p.lon)*scale*cos,ay=(a.lat-p.lat)*scale;
        double bx=(b.lon-p.lon)*scale*cos,by=(b.lat-p.lat)*scale;
        double dx=bx-ax,dy=by-ay,den=dx*dx+dy*dy;
        double t=den==0?0:Math.max(0,Math.min(1,-(ax*dx+ay*dy)/den));
        return Math.hypot(ax+t*dx,ay+t*dy);
    }
}
