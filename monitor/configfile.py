import configparser


class ConfigClass:
    '''
    Handle a .INI style configuration file.  Every function is designed to not
    crash, and always return a default of the items are not present.
    
    Currently support read-only.
    '''
    def __init__( self, filename ):
        self.filename = filename
        self.config   = configparser.ConfigParser()
        self.readFile = False
        self._openConfig()
        
    def _openConfig(self) -> None:
        '''
        Open,, and read in the configuation file.  If the file does not exist, keep
        trying to reopen the file until the file does exist.  While this approach does
        not always help, it does allow for an application polls the configuration file
        occasionally.
        
        '''
        try:
            _result = self.config.read( self.filename )
        except Exception as error:
            print( f"{error}" )
        
    def getValue( self, section : str, key : str, default="" ) -> str:
        '''
        This routine obtains the value of the key within the specified section, if there
        is such a item.
        
        Parameter:
            section - Name of the section to look for
            key     - Key of the value desired
            default - Value to return if there is no key
            
        Returns:
            The value of the key from the section read.
        '''
        value = default
        try:
            value = self.config[section][key].replace('"','').strip()
        except:
            value = default
        return value
    
    def getValueAsList( self, section : str, name : str, default = [] ) -> list[str]:
        '''
        This routine looks for the key in the specified section and returns the data if
        it exists, if not the default value is returned.
        
        Parameters:
            section - Section to look for
            key     - Key to return the value of
            default - If they key or section does not exist, return this value
            
        Returns:
            a List of items
        '''
        value = default
        try:
            temp = self.config[section][name]
            value = [ n.replace('"','').strip() for n in temp.split(",")]
        except:
            value = default
        return value
    
if __name__ == "__main__":
    cfg = ConfigClass( "test.ini" )
    print( f"Value = {cfg.getValue( 'temperature', 'ignore' )}" )
    print( f"Value = {cfg.getValueAsList( 'temperature', 'ignore' )}" )
    print( f"Value = {cfg.getValue( 'performance', 'ignore' )}" )
    print( f"Value = {cfg.getValueAsList( 'performance', 'ignore' )}" )

    drive = cfg.getValue( 'smartctl', 'sda' )
    print( f"drive = {drive}" )
    
    cfg = ConfigClass( "missingfile.ini" )
    
        
