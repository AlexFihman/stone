using System;

[Serializable]
public class line{
  public int size;

  public ulong[] data;

  public line(int aSize){
    size = aSize*2;
    data = new ulong[(size+63)/64];
  }

  public bool this[int i]{
  get{
    int p = i/64;
    int x = i%64;
    ulong l = data[p] >> x;
    return (l & 1) > 0;
    }
  set{
    int p = i/64;
    int x = i%64;
    data[p] |= 1UL << x;
    }
  }

  public void up(){
    int i;

    for (i = ((size + 63) / 64) - 1; i>=0; i--)
    {
      ulong r = data[i] << 1;
      if (i>0){
        r |= data[i-1] >> 63;
      }
      data[i] = r;
    }
  }

  public void dump(){
    Console.WriteLine("{0} {1}",data[0],data[1]);
  }
}