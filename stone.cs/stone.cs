using System;                              
using System.IO;
using System.IO.Compression;
using System.Runtime.Serialization.Formatters.Binary;
using System.Threading;


public class stone
{
    const String datadir = @"data/";
    const int arr_size = 10000;
    const int sq_size =  300000;

    static line[] L10;
    static line[] L10U;
    static line[] L11;
    static line[] L11U;
    static line L01;
    static line L01U;

    static int curr_line;
    static int x, y;

    // result IO
    static FileStream    stream;
    static BinaryWriter  writer;

    // tmp
    static BinaryFormatter formatter;

    // compressed holder output IO

    static FileStream   stream_w01;
    static FileStream   stream_w01U;
    static FileStream   stream_w11;
    static FileStream   stream_w11U;
    static GZipStream   zs_w01;
    static GZipStream   zs_w01U;
    static GZipStream   zs_w11;
    static GZipStream   zs_w11U;

    // compressed input IO
    static FileStream   stream_r01;
    static FileStream   stream_r01U;
    static FileStream   stream_r11;
    static FileStream   stream_r11U;
    static GZipStream   zs_r01;
    static GZipStream   zs_r01U;
    static GZipStream   zs_r11;
    static GZipStream   zs_r11U;

    public static void init(int pos)
    {
        L10 = new line[arr_size];
        L10U = new line[arr_size];
        L11 = new line[arr_size];
        L11U = new line[arr_size];
        int t;
        for (t = 0; t < arr_size; t++)
        {
            L10[t] = new line(sq_size);
            L10U[t] = new line(sq_size);
            L11[t] = new line(sq_size);
            L11U[t] = new line(sq_size);
        }
        L01 = new line(sq_size);
        L01U = new line(sq_size);
    }

    public static void propagate()
    {
        int i;
        for (y = 0; y < arr_size; y++)
        {
            i = 0;
            while ((L01.data[i] | L01U.data[i] | L10[y].data[i] | L10U[y].data[i] |
                   L11[y].data[i] | L11U[y].data[i]) == 18446744073709551615UL) i++;
	    i *= 64;
            while (L01[i] || L01U[i] || L10[y][i] || L10U[y][i] ||
                   L11[y][i] || L11U[y][i]) i++;
            if (x==1001 && y==1){
              Console.WriteLine("{0} {1} {2}", x, y, i);
            }

            writer.Write(i);
            L01[i] = true;
            L01U[i] = true;
            L10[y][i] = true;
            L10U[y][i] = true;
            L11[y][i] = true;
            L11U[y][i] = true;

            L01U.up();
            L10U[y].up();
            L11U[y].up();
        }
    }

    public static line read(GZipStream zs)
    {
        line ln;
        ln = (line)formatter.Deserialize(zs);
        return ln;
    }

    public static void write(GZipStream zs, line ln)
    {
        formatter.Serialize(zs, ln);
    }


    public static void line_up()
    {
        if (curr_line > 0)
        {
            L01  = read(zs_r01);
            L01U = read(zs_r01U);
            if (x>0){
              L11[0]  = read(zs_r11);
              L11U[0] = read(zs_r11U);
            } else 
            {
              L11[0]  = new line(sq_size);
              L11U[0] = new line(sq_size);
            }
        }
        else 
        { 
            L01 = new line(sq_size);
            L01U = new line(sq_size);
            L11[0] = new line(sq_size);
            L11U[0] = new line(sq_size);
        }

        for (y = 0; y < arr_size; y++)
        {
            propagate();
        }

        int curr_line2 = curr_line + arr_size;

        write(zs_w01, L01);
        write(zs_w01U, L01U);
        write(zs_w11, L11[arr_size - 1]);
        write(zs_w11U, L11U[arr_size - 1]);

        int i;
        for (i = arr_size-1; i > 0; i--) 
        {
            L11[i] = L11[i - 1];
            L11U[i] = L11U[i - 1];
        }
        L11[0] = new line(sq_size);
        L11U[0] = new line(sq_size);
    }

    public static void Main()
    {
        curr_line = Int32.Parse(Environment.GetCommandLineArgs()[1]);
        //tmp
        formatter = new BinaryFormatter();
        // result output
        stream = new FileStream(datadir + "res" + curr_line + ".bin", FileMode.Create);
        writer = new BinaryWriter(stream);

        // holder writers
        stream_w01 = new FileStream(datadir + "01_." + (curr_line+arr_size) + ".dat", FileMode.Create, FileAccess.Write);
        stream_w01U= new FileStream(datadir + "01U." + (curr_line+arr_size) + ".dat", FileMode.Create, FileAccess.Write);
        stream_w11 = new FileStream(datadir + "11_." + (curr_line+arr_size) + ".dat", FileMode.Create, FileAccess.Write);
        stream_w11U= new FileStream(datadir + "11U." + (curr_line+arr_size) + ".dat", FileMode.Create, FileAccess.Write);
        zs_w01 = new GZipStream(stream_w01 ,CompressionMode.Compress,true);
        zs_w01U= new GZipStream(stream_w01U,CompressionMode.Compress,true);
        zs_w11 = new GZipStream(stream_w11 ,CompressionMode.Compress,true);
        zs_w11U= new GZipStream(stream_w11U,CompressionMode.Compress,true);

        // holder readers
        if (curr_line>0)
        {
          stream_r01 = new FileStream(datadir + "01_." + curr_line + ".dat", FileMode.Open, FileAccess.Read);
          stream_r01U= new FileStream(datadir + "01U." + curr_line + ".dat", FileMode.Open, FileAccess.Read);
          stream_r11 = new FileStream(datadir + "11_." + curr_line + ".dat", FileMode.Open, FileAccess.Read);
          stream_r11U= new FileStream(datadir + "11U." + curr_line + ".dat", FileMode.Open, FileAccess.Read);
          zs_r01 = new GZipStream(stream_r01 ,CompressionMode.Decompress,true);
          zs_r01U= new GZipStream(stream_r01U,CompressionMode.Decompress,true);
          zs_r11 = new GZipStream(stream_r11 ,CompressionMode.Decompress,true);
          zs_r11U= new GZipStream(stream_r11U,CompressionMode.Decompress,true);
        }

        init(0);
        for (x = 0; x < sq_size; x++)
        {
            line_up();
            Console.WriteLine(x);
        }
        writer.Close();
        stream.Close();

        zs_w01.Close();
        zs_w01U.Close();
        zs_w11.Close();
        zs_w11U.Close();
        stream_w01.Close();
        stream_w01U.Close();
        stream_w11.Close();
        stream_w11U.Close();

        if (curr_line>0)
        {
          zs_r01.Close();
          zs_r01U.Close();
          zs_r11.Close();
          zs_r11U.Close();
          stream_r01.Close();
          stream_r01U.Close();
          stream_r11.Close();
          stream_r11U.Close();
        }
    }

}